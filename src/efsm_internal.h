/**
 * \file efsm_internal.h
 * \brief The internal interface for efsm
 * \author Jason Carey
 *
 * these are the internal data structures for efsm.  They're exposed in this
 * header to ease in testing
 *
 * \see efsm.h
 */
#include "efsm.h"

/** Valid fsa statuses */
enum efsm__fsa_status {
    EFSM_FSA_NEW,               // New fsa or with a new message in an iteration of run
    EFSM_FSA_ACTIVE,            // Has messages in queue
    EFSM_FSA_INACTIVE,          // no messages in queue
};

struct efsm;
struct efsm__fsa;
struct efsm__msg;

/** Transitions between state A -> B */
typedef struct efsm__transition {
    int msg_type;               // The msg type to match from efsm_fsa_send
    efsm_cb_t code;             // callback that's run
    void *data;                 // parameter to the callbac
    int next_state;             // Where we transition to
} efsm__transition_t;

/** States, which are really just a list of possible transitions */
typedef struct efsm__state {
    int n_transitions;
    efsm__transition_t *transitions;
} efsm__state_t;

/** The internal efsm struct */
typedef struct efsm_ {
    int n_states;
    struct efsm__state *states;

    struct efsm__fsa *queued;
    struct efsm__fsa *actives;
    struct efsm__fsa *inactives;

    /** An optional callback for each transition */
    efsm_transition_cb_t transition_cb;
} efsm__t;

/** The internal efsm_fsa struct */
typedef struct efsm__fsa {
    struct efsm_ *efsm;
    int state;
    void *data;

    /** The externally visible object */
    efsm_fsa_t *wrapper;

    enum efsm__fsa_status status;

    efsm_fsa_dcb_t dcb;

    /** A list of all queued messages */
    struct efsm__msg *queued;

    /** Based on status, the pointers for the list we're in */
    struct efsm__fsa *next, *prev;
} efsm__fsa_t;

/** Internal packaging for a message */
typedef struct efsm__msg {
    int type;
    void *data;

    struct efsm__fsa *fsa;

    /** The pointers for the linked list we're in in our parent fsa */
    struct efsm__msg *next, *prev;
} efsm__msg_t;
