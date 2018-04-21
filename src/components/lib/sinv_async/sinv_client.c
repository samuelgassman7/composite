#include <sinv_async.h>

#include <sl.h>
#include <../interface/capmgr/capmgr.h>
#include <../interface/channel/channel.h>

#define SINV_SRV_POLL_US 1000
#define USEC_2_CYC 2800 /* TODO: move out some generic parts from sl */

void
sinv_client_init(struct sinv_async_info *s, cos_channelkey_t shm_key)
{
	cbuf_t id;
	vaddr_t addr = 0;

	memset(s, 0, sizeof(struct sinv_async_info));

	id = channel_shared_page_allocn(shm_key, SINV_INIT_NPAGES, &addr);
	assert(id && addr);

	s->init_key     = shm_key;
	s->init_shmaddr = addr;
}

int
sinv_client_thread_init(struct sinv_async_info *s, thdid_t tid, cos_channelkey_t rcvkey, cos_channelkey_t skey)
{
	volatile unsigned long *reqaddr = (volatile unsigned long *)(s->init_shmaddr);
	int *retval = (int *)(reqaddr + 1), ret;
	struct sinv_thdcrt_req *req = (struct sinv_thdcrt_req *)(reqaddr + 2);
	struct sinv_thdinfo *tinfo = &s->cdata.cthds[tid];
	vaddr_t shmaddr = 0;
	cbuf_t id = 0;
	asndcap_t snd = 0;
	arcvcap_t rcv = 0;
	spdid_t child = cos_inv_token() == 0 ? cos_spd_id() : (spdid_t)cos_inv_token();

	assert(ps_load((unsigned long *)reqaddr) == SINV_REQ_RESET);

	req->clspdid = child; /* this is done from the scheduler on invocation */
	req->rkey = rcvkey;
	req->skey = skey;

	id = channel_shared_page_allocn(skey, SINV_REQ_NPAGES, &shmaddr);
	assert(id && shmaddr);

	if (rcvkey) {
		/* capmgr interface to create a rcvcap for "tid" thread in the scheduler component..*/
		rcv = capmgr_rcv_create(child, tid, rcvkey, 0, 0); /* TODO: rate- limit */
		assert(rcv);
	}

	ret = ps_cas((unsigned long *)reqaddr, SINV_REQ_RESET, SINV_REQ_SET); /* indicate request available */
	assert(ret);
	/* TODO: cos_asnd */

	/* TODO: cos_rcv! */
	while (ps_load((unsigned long *)reqaddr) != SINV_REQ_RESET) {
		cycles_t now, timeout;

		rdtscll(now);
		timeout = now + (SINV_SRV_POLL_US * USEC_2_CYC);
	//	sl_thd_block_timeout(0, timeout); /* called from the scheduler */
	}

	/* TODO: UNDO!!! */
	if (*retval) return *retval;

	snd = capmgr_asnd_key_create(skey);
	assert(snd);

	tinfo->rkey     = rcvkey;
	tinfo->skey     = skey;
	tinfo->clientid = child;
	tinfo->sndcap   = snd;
	tinfo->rcvcap   = rcv; /* cos_rcv in the scheduler */
	tinfo->shmaddr  = shmaddr;

	return 0;
}

static int
sinv_client_call_wrets(int wrets, struct sinv_async_info *s, sinv_num_t n, word_t a, word_t b, word_t c, word_t *r2, word_t *r3)
{
	struct sinv_thdinfo *tinfo = &s->cdata.cthds[cos_thdid()];
	volatile unsigned long *reqaddr = (volatile unsigned long *)tinfo->shmaddr;
	int *retval = NULL, ret;
	struct sinv_call_req *req = NULL;

	assert(n >= 0 && n < SINV_NUM_MAX);
	assert(reqaddr);

	retval = (int *)(reqaddr + 1);
	req    = (struct sinv_call_req *)(reqaddr + 2);

	req->callno = n;
	req->arg1   = a;
	req->arg2   = b;
	req->arg3   = c;

	ret = ps_cas((unsigned long *)reqaddr, SINV_REQ_RESET, SINV_REQ_SET);
	assert(ret); /* must be sync.. */

	/* TODO: use the scheduler's rate-limiting api */
	/* cos_asnd(tinfo->sndcap, 0); */

	while (ps_load((unsigned long *)reqaddr) != SINV_REQ_RESET) {// || !tinfo->rcvcap || cos_rcv(tinfo->rcvcap, RCV_NON_BLOCKING, NULL) < 0) {
		cycles_t now, timeout;

		rdtscll(now);
		timeout = now + (SINV_SRV_POLL_US * USEC_2_CYC);
	//	sl_thd_block_timeout(0, timeout); /* in the scheduler component */
	}

	assert(ps_load((unsigned long *)reqaddr) == SINV_REQ_RESET);
	if (!wrets) goto done;

	*r2 = req->ret2;
	*r3 = req->ret3;

done:
	return *retval;
}

int
sinv_client_call(struct sinv_async_info *s, sinv_num_t n, word_t a, word_t b, word_t c)
{
	return sinv_client_call_wrets(0, s, n, a, b, c, NULL, NULL);
}

int
sinv_client_rets_call(struct sinv_async_info *s, sinv_num_t n, word_t *r2, word_t *r3, word_t a, word_t b, word_t c)
{
	return sinv_client_call_wrets(1, s, n, a, b, c, r2, r3);
}
