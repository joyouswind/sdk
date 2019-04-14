#include "ice-checklist.h"
#include "ice-candidates.h"
#include "stun-internal.h"

static int ice_gather_onbind(void* param, const stun_transaction_t* req, const stun_transaction_t* resp, int code, const char* phrase)
{
	int i, r, protocol;
	struct ice_checklist_t* l;
	struct ice_candidate_t c, *local;

	l = (struct ice_checklist_t*)param;

	if (code >= 200 && code < 300)
	{
		memset(&c, 0, sizeof(struct ice_candidate_t));
		c.type = ICE_CANDIDATE_SERVER_REFLEXIVE;
		stun_transaction_getaddr(resp, &protocol, &c.base, &c.stun, &c.addr);
		ice_candidate_priority(&c);
		ice_candidate_foundation(&c);

		for (i = 0; i < ice_candidates_count(&l->locals); i++)
		{
			local = ice_candidates_get(&l->locals, i);
			if (0 == socket_addr_compare(&local->addr, &c.base) && 0 == socket_addr_compare(&local->stun, &c.stun))
			{
				c.componentId = local->componentId;
				break;
			}
		}

		r = ice_checklist_add_local_candidate(l, &c);
	}
	else
	{
		r = 0; // ignore gather error
		printf("ice_checklist_ongather code: %d, phrase: %s\n", code, phrase);
	}

	darray_erase2(&l->gathers, req, NULL);
	if (0 == darray_count(&l->gathers))
		return l->ongather(l->ongatherparam, 0);
	return 0;
}

/// Gather server reflexive and relayed candidates
int ice_checklist_gather_stun_candidate(struct ice_checklist_t* l, ice_agent_ongather ongather, void* param)
{
	int i, r;
	struct stun_transaction_t* req;
	struct ice_candidate_t *p, *pc = NULL;
	
	l->gathers.count = 0; // reset gather array
	l->ongatherparam = param;
	l->ongather = ongather;

	for (i = 0; i < ice_candidates_count(&l->locals); i++)
	{
		p = ice_candidates_get(&l->locals, i);
		if (p->type != ICE_CANDIDATE_HOST || 0 == p->stun.ss_family)
			continue;

		req = stun_transaction_create(l->stun, STUN_RFC_5389, ice_gather_onbind, l);
		if (!req) continue;

		stun_transaction_setaddr(req, STUN_PROTOCOL_UDP, &p->addr, &p->stun);
		stun_transaction_setauth(req, l->auth->credential, l->auth->usr, l->auth->pwd, l->auth->realm, l->auth->nonce);
		r = stun_agent_bind(req);
		if (0 != r)
		{
			stun_transaction_destroy(req);
			continue;
		}

		darray_push_back(&l->gathers, &req, 1);
	}

	// empty?
	if (0 == darray_count(&l->gathers))
		return l->ongather(l->ongatherparam, 0);
	return 0;
}