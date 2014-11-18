/**
 * \file efsm.h
 * \brief Event based Finite State Machine
 * \author Jason Carey
 *
 * efsm is a library for managing finite state machines that transition via
 * generic messages.  Each machine has a defined set of states and transitions
 * that are valid and any number of finite state automatons that traverse those
 * paths.  Automatons can receive messages into their mailboxes via
 * efsm_fsa_send and process those messages when efsm_run is called.  As
 * transitions occur, callbacks are invoked and those callbacks can in turn
 * generate events.  Alternatively, events can be driven externally, I.e.
 * pollin or pollout from poll on an fd.
 *
 * Philosophically, the library is built around the idea that all event
 * generators should be first class, rather than having special code for
 * managing errors or managing transitions that occur due to callback code.
 * Instead, we have a robust idiom for talking about transitions (edge
 * triggered from events) and a simple api for indicating direction (sending
 * messages).  Enforcing strict transition rules between states makes the full
 * machine easy to reason about and even easy to print (efsm_pp can be used to
 * generate graphs of a given efsm).  This makes it easy to grasp the full fsm
 * and makes it less error prone to extend or alter a working piece of code
 * (since you can visually or even programatically verify that events are
 * handled)
 *
 * The main API is:
 *
 *   efsm_t * efsm = efsm_new(rules, &efsm_opts);
 *
 *   efsm_fsa_t * fsa = efsm_fsa_new(efsm, &fsa_opts)
 *
 *   efsm_fsa_send(fsa, MY_MESSAGE, &ctx);
 *
 *   efsm_destroy(efsm);
 */
#ifndef EFSM_H
#define EFSM_H

typedef struct efsm {
    void *data;
} efsm_t;

typedef struct efsm_fsa {
    void *data;
} efsm_fsa_t;

/**
 * efsm transition code callback
 *
 * @return 0  - sucess
 *         -1 - error
 *         1  - destroy
 */
typedef int (*efsm_cb_t) (efsm_fsa_t * fsa, void *fsa_data,
                          void *transition_data, int type, void *msg_data);

typedef void (*efsm_transition_cb_t) (int pre_state, int msg, int post_state);

typedef void (*efsm_fsa_dcb_t) (void *data);

typedef struct efsm_transition_rules {
    int current_state;
    int msg_type;
    efsm_cb_t code;
    void *data;
    int next_state;
} efsm_transition_rules_t;

typedef struct efsm_opts {
    efsm_transition_cb_t transition_cb;
} efsm_opts_t;

typedef struct efsm_fsa_opts {
    void *hint;
    efsm_fsa_dcb_t destroy_cb;
} efsm_fsa_opts_t;

/** sends a message to a fsa
 *
 * Messages aren't processed directly out of this function, efsm_run must be
 * called before they will be consumed
 *
 * \param type the message type
 * \param data an opaque pointer that will be made available in the transition callback
 *
 * \return 0 for success, -1 for failure
 */
int efsm_fsa_send(efsm_fsa_t * fsa, int type, void *data);

/** Creates a new fsa
 *
 * The standard invocation looks like:
 *   efsm_fsa_opts_t opts = { ... };
 *   efsm_fsa_new(efsm, initial_state, &opts);
 *
 * \param state The initial state for the fsa
 * \param opts Options for the new fsa.  This can be NULL
 */
efsm_fsa_t *efsm_fsa_new(efsm_t * efsm, int state, efsm_fsa_opts_t * opts);

/** destroys a fsa
 *
 * This will call the fsa's destroy callback if one was supplied
 *
 * \see efsm_fsa_opts_t
 */
void efsm_fsa_destroy(efsm_fsa_t * fsa);

/** creates a new efsm
 *
 * The standard invocation looks like:
 *
 * efsm_transition_rules_t rules[] = {
 *   { ... },
 *   { -1 },
 * };
 * efsm_opts_t opts = { ... };
 * efsm_new(rules, opts);
 *
 * \param rules an array of transition_rules ending with a rule with a state of -1
 * \param opts an optional pointer with parameters to new
 */
efsm_t *efsm_new(efsm_transition_rules_t * rules, efsm_opts_t * opts);

/** destroys an efsm
 * 
 * This calls efsm_fsa_destroy on each current fsa before returning
 *
 * \see efsm_fsa_destroy
 */
void efsm_destroy(efsm_t * efsm);

/** runs the efsm
 * 
 * This loops over all active (I.e. with messages queued) fsa's in the efsm,
 * processing all of their input.  If fsa's send themselves or other fsa's
 * messages, the loop will have more to do after a full iteration
 *
 * \return 0 if no more work to do.  1 if more work to do. -1 on error
 */
int efsm_run(efsm_t * efsm);

/** pretty prints the efsm in dot notation
 *
 * This produces a string suitable to pass to graphviz in the form of dot syntax.
 *
 * I.e.
 *
 * digraph G {
 *   FOO -> BAR [label="BAZ"];
 * }
 *
 * \param state_names the names of the states used in the machine
 * \param transition_names the names of the messages used in the machine
 */
char *efsm_pp(efsm_t * efsm, char **state_names, char **transition_names);

#endif
