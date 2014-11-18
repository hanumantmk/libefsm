/**
 * \file libefsm.c
 * \brief Event based Finite State Machine
 * \author Jason Carey
 *
 * This file forms the implementation for the interface laid out in efsm.h
 *
 * Conventions to note:
 * * efsm__ namespace is reserved for internal implementation
 * * efsm_ namespace is used for externally visible types and functions
 *
 * Issues to note:
 * * efsm is not threadsafe
 * * efsm_run returns after each iteration of all available fsa's.  If run in a
 *   loop, that could run forever
 *
 */

#include <utlist.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <utstring.h>

#include "efsm.h"
#include "efsm_internal.h"

void efsm__fsa_destroy(efsm__fsa_t * fsa);
void efsm__msg_destroy(efsm__msg_t * msg);

/** Toggles the status of a fsa
 *
 * Status can be active, inactive or new.  These states correspond to fsa's
 * with messages in queue, those with no messages and new fsa's OR fsa's that
 * have received a message in this iteration of efsm_run.
 *
 * Toggle moves active or inactive fsa's into the queue and moves queued fsas
 * into the active or inactive lists
 */
static void efsm__fsa_toggle_status(efsm__fsa_t * fsa)
{
    if (fsa->status == EFSM_FSA_ACTIVE) {
        fsa->status = EFSM_FSA_NEW;

        DL_DELETE(fsa->efsm->actives, fsa);
        DL_APPEND(fsa->efsm->queued, fsa);
    } else if (fsa->status == EFSM_FSA_INACTIVE) {
        fsa->status = EFSM_FSA_NEW;

        DL_DELETE(fsa->efsm->inactives, fsa);
        DL_APPEND(fsa->efsm->queued, fsa);
    } else if (fsa->status == EFSM_FSA_NEW) {
        if (fsa->queued) {
            fsa->status = EFSM_FSA_ACTIVE;
            DL_DELETE(fsa->efsm->queued, fsa);
            DL_APPEND(fsa->efsm->actives, fsa);
        } else {
            fsa->status = EFSM_FSA_INACTIVE;
            DL_DELETE(fsa->efsm->queued, fsa);
            DL_APPEND(fsa->efsm->inactives, fsa);
        }
    } else {
        assert(0);
    }
}

int efsm_fsa_send(efsm_fsa_t * _fsa, int type, void *data)
{
    efsm__fsa_t *fsa = _fsa->data;

    efsm__msg_t *msg = calloc(sizeof(*msg), 1);

    msg->fsa = fsa;
    msg->data = data;
    msg->type = type;

    DL_APPEND(fsa->queued, msg);

    if (fsa->status == EFSM_FSA_INACTIVE)
        efsm__fsa_toggle_status(fsa);

    return 0;
}

/** processes all waiting messages for a given fsa
 *
 * o loops over all of the messages for a given fsa
 * o transitions based on message type
 * o calls transition callbacks with messages
 * o deletes the fsa if it's transitioning to floor
 * o returns -1 if an error occurs
 *   o no transition is available for a given message
 *   o a transition callback returns -1
 * o toggles the fsa back to wherever it needs to be
 */
int efsm__fsa_run(efsm__fsa_t * fsa)
{
    int i;
    int r;
    efsm__transition_t *transition;
    efsm__msg_t *ele, *tmp;

    DL_FOREACH_SAFE(fsa->queued, ele, tmp) {
        transition = NULL;
        efsm__state_t *state = fsa->efsm->states + fsa->state;
        for (i = 0; i < state->n_transitions; i++) {
            if (state->transitions[i].msg_type == ele->type) {
                transition = state->transitions + i;
                break;
            }
        }

        if (transition) {
            if (fsa->efsm->transition_cb)
                fsa->efsm->transition_cb(fsa->state, ele->type,
                                         transition->next_state);
            r = transition->code(fsa->wrapper, fsa->data, transition->data,
                                 ele->type, ele->data);

            if (r < 0) {
                return -1;
            } else if (r > 0) {
                if (transition->next_state != -1)
                    return -1;

                efsm__fsa_destroy(fsa);
                return 1;
            }
        } else {
            return -1;
        }

        fsa->state = transition->next_state;

        efsm__msg_destroy(ele);
    }

    efsm__fsa_toggle_status(fsa);

    return 0;
}

/* Toggles all of the queued fsas, then processes all of the actives
 */
int efsm_run(efsm_t * _efsm)
{
    int r;
    efsm__t *efsm = _efsm->data;

    efsm__fsa_t *ele, *tmp;

    DL_FOREACH_SAFE(efsm->queued, ele, tmp) {
        efsm__fsa_toggle_status(ele);
    }

    DL_FOREACH_SAFE(efsm->actives, ele, tmp) {
        r = efsm__fsa_run(ele);

        if (r < 0) {
            return -1;
        }
    }

    return efsm->queued ? 1 : 0;
}

/* This actually creates a wrapper shim (with an opaque handle to the internal
 * data structure) in addition to the internal object and returns that
 */
efsm_fsa_t *efsm_fsa_new(efsm_t * _efsm, int state, efsm_fsa_opts_t * opts)
{
    efsm__t *efsm = _efsm->data;

    efsm__fsa_t *fsa = calloc(sizeof(*fsa), 1);
    efsm_fsa_t *_fsa = calloc(sizeof(*_fsa), 1);
    _fsa->data = fsa;
    fsa->wrapper = _fsa;

    if (opts) {
        if (opts->hint)
            fsa->data = opts->hint;
        if (opts->destroy_cb)
            fsa->dcb = opts->destroy_cb;
    }

    fsa->state = state;
    fsa->efsm = efsm;
    fsa->status = EFSM_FSA_NEW;

    DL_APPEND(efsm->queued, fsa);

    return _fsa;
}

/** compiles the internal state/transition data structure from the external table API
 *
 * Assumes that rules ends with a transition with a starting state of -1.
 *
 * \param rules All of the states and their transitions in the efsm
 * \param[out] n_states The number of states represented in the transition rules
 */
efsm__state_t *efsm__states_from_rules(efsm_transition_rules_t * rules,
                                       int *n_states)
{
    int max_state = 0;

    efsm_transition_rules_t *r;

    // Find the max state
    for (r = rules; r->current_state != -1; r++) {
        if (r->current_state > max_state)
            max_state = r->current_state;
        if (r->next_state > max_state)
            max_state = r->next_state;
    }
    *n_states = max_state + 1;

    efsm__state_t *states = calloc(sizeof(*states), *n_states);

    // Loop over all of the states, finding all of the rules that match and
    // creating a second dimension of transition message id's
    int state;
    for (state = 0; state < *n_states; state++) {
        int transition_count = 0;
        for (r = rules; r->current_state != -1; r++)
            if (r->current_state == state)
                transition_count++;
        states[state].n_transitions = transition_count;
        states[state].transitions =
            calloc(sizeof(*(states[state].transitions)), transition_count);
        int i = 0;
        for (r = rules; r->current_state != -1; r++)
            if (r->current_state == state) {
                states[state].transitions[i].code = r->code;
                states[state].transitions[i].msg_type = r->msg_type;
                states[state].transitions[i].next_state = r->next_state;
                states[state].transitions[i].data = r->data;
                i++;
            }
    }
    return states;
}

efsm_t *efsm_new(efsm_transition_rules_t * rules, efsm_opts_t * opts)
{
    efsm_t *_efsm = calloc(sizeof(*_efsm), 1);
    efsm__t *efsm = calloc(sizeof(*efsm), 1);
    _efsm->data = efsm;

    if (opts) {
        efsm->transition_cb = opts->transition_cb;
    }

    efsm->states = efsm__states_from_rules(rules, &efsm->n_states);

    return _efsm;
}

void efsm_destroy(efsm_t * _efsm)
{
    efsm__t *efsm = _efsm->data;

    efsm__fsa_t *ele, *tmp;

    DL_FOREACH_SAFE(efsm->queued, ele, tmp) {
        efsm__fsa_destroy(ele);
    }
    DL_FOREACH_SAFE(efsm->actives, ele, tmp) {
        efsm__fsa_destroy(ele);
    }
    DL_FOREACH_SAFE(efsm->inactives, ele, tmp) {
        efsm__fsa_destroy(ele);
    }

    int i;
    for (i = 0; i < efsm->n_states; i++) {
        free(efsm->states[i].transitions);
    }

    free(efsm->states);

    free(efsm);
    free(_efsm);
}

void efsm_fsa_destroy(efsm_fsa_t * fsa)
{
    efsm__fsa_destroy((efsm__fsa_t *) fsa->data);
}

/** destroys the internal representation of a fsa
 *
 * calls the destroy callback if one was provided
 */
void efsm__fsa_destroy(efsm__fsa_t * fsa)
{
    efsm__msg_t *ele, *tmp;

    if (fsa->status == EFSM_FSA_ACTIVE) {
        DL_DELETE(fsa->efsm->actives, fsa);
    } else if (fsa->status == EFSM_FSA_INACTIVE) {
        DL_DELETE(fsa->efsm->inactives, fsa);
    } else if (fsa->status == EFSM_FSA_NEW) {
        DL_DELETE(fsa->efsm->queued, fsa);
    } else {
        // we shouldn't get here
        assert(0);
    }

    DL_FOREACH_SAFE(fsa->queued, ele, tmp) {
        efsm__msg_destroy(ele);
    }

    if (fsa->dcb)
        fsa->dcb(fsa->data);

    free(fsa->wrapper);

    free(fsa);
}

/** Destroy an efsm message */
void efsm__msg_destroy(efsm__msg_t * msg)
{
    DL_DELETE(msg->fsa->queued, msg);

    free(msg);
}

char *efsm_pp(efsm_t * _efsm, char **state_names, char **transition_names)
{
    UT_string s;
    utstring_init(&s);

    utstring_printf(&s, "digraph G {\n");

    efsm__t *efsm = _efsm->data;

    int i, j;
    for (i = 0; i < efsm->n_states; i++) {
        efsm__state_t *state = efsm->states + i;
        for (j = 0; j < state->n_transitions; j++) {
            efsm__transition_t *transition = state->transitions + j;

            char *cur_state = state_names[i];
            char *next_state =
                transition->next_state !=
                -1 ? state_names[transition->next_state] : "_";
            char *tname = transition_names[transition->msg_type];

            utstring_printf(&s, "  %s -> %s [label=\"%s\"];\n", cur_state,
                            next_state, tname);
        }
    }

    utstring_printf(&s, "}\n");

    char *out = utstring_body(&s);

    return out;
}
