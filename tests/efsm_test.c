/**
 * \file efsm_test.cpp
 * \brief Tests for efsm
 * \author Jason Carey
 *
 * Basic exercise of the efsm library
 *
 * \see efsm.h
 */

#include <efsm.h>
#include <efsm_internal.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#define ASIZE(a) (sizeof(a) / sizeof(*a))

/** The states we can be in */
enum STATES {
    STATE_A,
    STATE_B,
    STATE_DESTROY,
};

/** Their string representations */
const char *STATES_strings[] = {
    "STATE_A",
    "STATE_B",
    "STATE_DESTROY",
};

/** The messages we can send */
enum MSGS {
    MSG_A,
    MSG_B,
    MSG_DESTROY,
};

/** Their string representations */
const char *MSGS_strings[] = {
    "MSG_A",
    "MSG_B",
    "MSG_DESTROY",
};

/** The expected graph output of efsm_pp */
#define GRAPH \
  "digraph G {\n" \
  "  STATE_A -> STATE_B [label=\"MSG_A\"];\n" \
  "  STATE_B -> STATE_DESTROY [label=\"MSG_B\"];\n" \
  "  STATE_DESTROY -> _ [label=\"MSG_DESTROY\"];\n" \
  "}\n"

/** Callback going from state_a --msg_a--> */
int state_a_on_msg_a(efsm_fsa_t * fsa, void *fsa_data, void * transition_data, int type, void *msg_data)
{
    efsm_fsa_send(fsa, MSG_B, NULL);
    return 0;
}

/** Callback going from state_b --msg_b--> */
int state_b_on_msg_b(efsm_fsa_t * fsa, void *fsa_data, void * transition_data, int type, void *msg_data)
{
    efsm_fsa_send(fsa, MSG_DESTROY, NULL);
    return 0;
}

/** Callback going from state_destroy --msg_destroy--> */
int state_destroy_on_msg_destroy(efsm_fsa_t * fsa, void *fsa_data, void * transition_data, int type,
                                 void *msg_data)
{
    return 1;
}

int main(void) {
   /** Our main test case */
    efsm_transition_rules_t rules[] = {
        {STATE_A, MSG_A, &state_a_on_msg_a, NULL, STATE_B},
        {STATE_B, MSG_B, &state_b_on_msg_b, NULL, STATE_DESTROY},
        {STATE_DESTROY, MSG_DESTROY, &state_destroy_on_msg_destroy, NULL, -1},
        {-1},
    };

    efsm_t *efsm = efsm_new(rules, NULL);

    efsm_fsa_t * fsa;

    efsm_fsa_opts_t opts = { 0 };
    opts.hint = &fsa;

    fsa = efsm_fsa_new(efsm, STATE_A, &opts);
    efsm__fsa_t * _fsa = (efsm__fsa_t *)fsa->data;

    assert(_fsa->state == STATE_A);
    efsm_fsa_send(fsa, MSG_A, NULL);
    assert(_fsa->state == STATE_A);

    int r;
    r = efsm_run(efsm);
    assert(r == 1);
    assert(_fsa->state == STATE_B);
    r = efsm_run(efsm);
    assert(r == 1);
    assert(_fsa->state == STATE_DESTROY);
    r = efsm_run(efsm);
    assert(r == 0);

    char * pp_graph = efsm_pp(efsm, (char **)STATES_strings, (char **)MSGS_strings);
    printf("%s\n", pp_graph);
    assert(strcmp(pp_graph, GRAPH) == 0);

    efsm_destroy(efsm);
}
