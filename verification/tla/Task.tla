-------------------------------- MODULE Task --------------------------------
(***************************************************************************)
(* Model of the task lifecycle shared by both backends:                    *)
(*   interpreter  src/backends/interpreter/builtins/concurrency.c          *)
(*                (spawn_task, task_thread_main, builtin_join/detach) and  *)
(*                task_release/task_free in values.c;                      *)
(*   compiled     runtime/src/builtins_async.c (task_alloc, task_launch,   *)
(*                task_thread_main, hml_join, hml_detach) and the          *)
(*                HML_VAL_TASK cases of hml_release/task_free in value.c.  *)
(*                                                                         *)
(* One task, its worker thread, and NHolders threads that each own one     *)
(* handle (copies of the spawn() result, e.g. sent over a channel) and do  *)
(* one of join / detach / nothing with it before dropping it. The handle   *)
(* reference count, the task mutex, the joined/detached claim flags and    *)
(* task_free's "detach if nobody did" safety net are modelled step for     *)
(* step; ref-count updates are atomic read-modify-writes as in the code.   *)
(*                                                                         *)
(* The pthread itself is modelled with POSIX's rules, and breaking one is  *)
(* an assertion failure: pthread_join or pthread_detach on a thread that   *)
(* was already detached or joined is undefined behavior (the thread ID may *)
(* have been reused). A thread's resources are reclaimed when it has       *)
(* exited and been joined or detached; one that never is, is leaked.       *)
(*                                                                         *)
(* Every access to the Task struct asserts it has not been freed.          *)
(***************************************************************************)
EXTENDS Naturals, Sequences, FiniteSets, TLC

CONSTANTS NHolders,      \* threads holding a handle (0 for detach(fn, ...))
          Backend,       \* "interp" or "compiled": they differ in spawn paths
          SpawnDetached, \* detach(fn, ...): fire and forget, no handle
          CreateFails    \* pthread_create fails

ASSUME Backend \in {"interp", "compiled"}
ASSUME SpawnDetached => NHolders = 0

Spawner == 0
Worker  == 1000
Holders == 1..NHolders

(* --algorithm task
variables
    \* Task struct
    allocated = FALSE,
    freed = FALSE,
    frees = 0,                 \* task_free() calls
    rc = 0,                    \* ref_count
    tmtx = 0,                  \* task mutex: 0 or holder
    joined = FALSE,
    detached = FALSE,
    tstate = "ready",          \* READY / RUNNING / COMPLETED
    hasThread = FALSE,         \* pthread_create succeeded
    \* The pthread, with POSIX semantics
    thrState = "none",         \* none / running / exited
    thrDetached = FALSE,       \* pthread_detach'ed (or created detached)
    thrJoined = FALSE,         \* pthread_join'ed
    \* Outcomes
    published = FALSE,         \* handles handed to the holders
    joinOk = {},               \* holders whose join() returned the result
    errors = [h \in Holders |-> "none"];

define
    Reclaimed == thrState = "exited" /\ (thrDetached \/ thrJoined)

    \* --- safety ---
    FreedAtMostOnce == frees <= 1
    RefCountSane == rc >= 0 /\ (freed => rc = 0)
    \* At most one join() succeeds.
    OneJoin == Cardinality(joinOk) <= 1
    \* A task is never both joined and detached.
    JoinXorDetach == ~(joined /\ detached)
    \* Every handle-holder's error is one the API documents for its call.
    ErrorsDocumented == \A h \in Holders : errors[h] \in
        {"none", "already joined", "join detached", "detach joined", "already detached"}

    \* --- end state (checked as invariants of the terminal states) ---
    AllDone == \A p \in {Spawner, Worker} \cup Holders : pc[p] = "Done"
    \* No Task leak, no pthread leak, no dangling reference.
    NoLeak == AllDone => (allocated => freed) /\ rc = 0 /\ (hasThread => Reclaimed)
end define;

\* ---- POSIX pthread operations (UB is an assertion failure) ----

macro pthread_detach() begin
    assert ~thrDetached /\ ~thrJoined;          \* else undefined behavior
    thrDetached := TRUE;
end macro;

\* task_free(): the "detach if nobody joined or detached" safety net, then
\* the struct is gone. Reads the flags without the task mutex: only the
\* thread that dropped the last reference can be here.
procedure task_free() begin
  TF_detach:
    assert ~freed;
    if hasThread /\ ~joined /\ ~detached then
        pthread_detach();
    end if;
  TF_free:
    assert ~freed;
    freed := TRUE;
    frees := frees + 1;
    return;
end procedure;

\* task_release() / hml_release(): atomic decrement; the thread that takes
\* the count to zero frees.
procedure release()
variables last = FALSE;
begin
  REL_dec:
    assert ~freed /\ rc > 0;
    rc := rc - 1;
    last := (rc = 0);   \* PlusCal: reads the value just written
  REL_free:
    if last then
        call task_free();
    end if;
  REL_ret:
    return;
end procedure;

\* hml_detach(handle) / builtin_detach(handle).
procedure detach_op(caller) begin
  D_lock:
    assert ~freed;
    await tmtx = 0; tmtx := self;
  D_check:
    if joined then
        tmtx := 0;
        if caller \in Holders then errors[caller] := "detach joined"; end if;
        return;
    elsif detached then
        tmtx := 0;
        if caller \in Holders then errors[caller] := "already detached"; end if;
        return;
    end if;
  D_claim:
    detached := TRUE;
    tmtx := 0;
  D_pthread:
    assert ~freed;                              \* reads task->thread
    pthread_detach();
    return;
end procedure;

process spawner = Spawner
begin
  SP_alloc:
    \* interp: task_new() gives 1, spawn_task retains for the worker;
    \* compiled: task_alloc() sets 2. Same net effect.
    allocated := TRUE;
    rc := 2;
    if SpawnDetached /\ Backend = "interp" then
        detached := TRUE;                       \* task->detached = opts->detached
    end if;
  SP_create:
    if CreateFails then
        \* interp: thread = NULL, release twice; compiled: detached = 1,
        \* ref_count = 1, release once. Either way: freed, no pthread.
        if Backend = "compiled" then
            detached := TRUE; rc := 1;
        else
            rc := 1;
        end if;
        call release();
        goto SP_done;
    else
        hasThread := TRUE;
        thrState := "running";
        if SpawnDetached /\ Backend = "interp" then
            thrDetached := TRUE;                \* PTHREAD_CREATE_DETACHED
        end if;
    end if;
  SP_detach_fn:
    if SpawnDetached then
        if Backend = "compiled" then
            \* detach(fn, ...) compiles to hml_spawn + hml_detach + hml_release.
            call detach_op(Spawner);
        end if;
    else
        goto SP_publish;
    end if;
  SP_drop_detached:
    call release();                             \* drop the spawn() handle
    goto SP_done;
  SP_publish:
    \* The spawner's handle becomes NHolders handles: retain for each copy
    \* beyond the first, or drop it if nobody keeps one.
    if NHolders = 0 then
        call release();
    else
        rc := rc + (NHolders - 1);
    end if;
  SP_published:
    published := TRUE;
  SP_done:
    skip;
end process;

process worker = Worker
begin
  W_start:
    \* No thread if pthread_create failed: nothing for this process to do.
    await thrState = "running" \/ pc[Spawner] = "Done";
    if thrState = "none" then goto W_end; end if;
  W_running:
    assert ~freed;
    await tmtx = 0;                             \* lock; set state; unlock
    tstate := "running";
  W_complete:
    \* (the function body runs here)
    assert ~freed;
    await tmtx = 0;                             \* lock; set state; unlock
    tstate := "completed";
  W_release:
    call release();                             \* the worker's reference
  W_exit:
    thrState := "exited";
  W_end:
    skip;
end process;

process holder \in Holders
variables claimed = FALSE;
begin
  H_start:
    await published;
    either
        \* join(task) / await
      H_join_lock:
        assert ~freed;
        await tmtx = 0; tmtx := self;
      H_join_check:
        if joined then
            errors[self] := "already joined";
        elsif detached then
            errors[self] := "join detached";
        else
            joined := TRUE; claimed := TRUE;
        end if;
        tmtx := 0;
      H_pthread_join:
        if claimed then
            assert ~freed;                      \* reads task->thread
            assert ~thrDetached /\ ~thrJoined;  \* else undefined behavior
            await thrState = "exited";
            thrJoined := TRUE;
      H_read_result:
            \* The worker has exited: its outcome is final and ours to read.
            assert ~freed /\ tstate = "completed";
            joinOk := joinOk \cup {self};
        end if;
    or
        call detach_op(self);
    or
        skip;                                   \* just drop the handle
    end either;
  H_drop:
    call release();
end process;

end algorithm; *)
\* BEGIN TRANSLATION
CONSTANT defaultInitValue
VARIABLES pc, allocated, freed, frees, rc, tmtx, joined, detached, tstate, 
          hasThread, thrState, thrDetached, thrJoined, published, joinOk, 
          errors, stack

(* define statement *)
Reclaimed == thrState = "exited" /\ (thrDetached \/ thrJoined)


FreedAtMostOnce == frees <= 1
RefCountSane == rc >= 0 /\ (freed => rc = 0)

OneJoin == Cardinality(joinOk) <= 1

JoinXorDetach == ~(joined /\ detached)

ErrorsDocumented == \A h \in Holders : errors[h] \in
    {"none", "already joined", "join detached", "detach joined", "already detached"}


AllDone == \A p \in {Spawner, Worker} \cup Holders : pc[p] = "Done"

NoLeak == AllDone => (allocated => freed) /\ rc = 0 /\ (hasThread => Reclaimed)

VARIABLES last, caller, claimed

vars == << pc, allocated, freed, frees, rc, tmtx, joined, detached, tstate, 
           hasThread, thrState, thrDetached, thrJoined, published, joinOk, 
           errors, stack, last, caller, claimed >>

ProcSet == {Spawner} \cup {Worker} \cup (Holders)

Init == (* Global variables *)
        /\ allocated = FALSE
        /\ freed = FALSE
        /\ frees = 0
        /\ rc = 0
        /\ tmtx = 0
        /\ joined = FALSE
        /\ detached = FALSE
        /\ tstate = "ready"
        /\ hasThread = FALSE
        /\ thrState = "none"
        /\ thrDetached = FALSE
        /\ thrJoined = FALSE
        /\ published = FALSE
        /\ joinOk = {}
        /\ errors = [h \in Holders |-> "none"]
        (* Procedure release *)
        /\ last = [ self \in ProcSet |-> FALSE]
        (* Procedure detach_op *)
        /\ caller = [ self \in ProcSet |-> defaultInitValue]
        (* Process holder *)
        /\ claimed = [self \in Holders |-> FALSE]
        /\ stack = [self \in ProcSet |-> << >>]
        /\ pc = [self \in ProcSet |-> CASE self = Spawner -> "SP_alloc"
                                        [] self = Worker -> "W_start"
                                        [] self \in Holders -> "H_start"]

TF_detach(self) == /\ pc[self] = "TF_detach"
                   /\ Assert(~freed, 
                             "Failure of assertion at line 93, column 5.")
                   /\ IF hasThread /\ ~joined /\ ~detached
                         THEN /\ Assert(~thrDetached /\ ~thrJoined, 
                                        "Failure of assertion at line 84, column 5 of macro called at line 95, column 9.")
                              /\ thrDetached' = TRUE
                         ELSE /\ TRUE
                              /\ UNCHANGED thrDetached
                   /\ pc' = [pc EXCEPT ![self] = "TF_free"]
                   /\ UNCHANGED << allocated, freed, frees, rc, tmtx, joined, 
                                   detached, tstate, hasThread, thrState, 
                                   thrJoined, published, joinOk, errors, stack, 
                                   last, caller, claimed >>

TF_free(self) == /\ pc[self] = "TF_free"
                 /\ Assert(~freed, 
                           "Failure of assertion at line 98, column 5.")
                 /\ freed' = TRUE
                 /\ frees' = frees + 1
                 /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
                 /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
                 /\ UNCHANGED << allocated, rc, tmtx, joined, detached, tstate, 
                                 hasThread, thrState, thrDetached, thrJoined, 
                                 published, joinOk, errors, last, caller, 
                                 claimed >>

task_free(self) == TF_detach(self) \/ TF_free(self)

REL_dec(self) == /\ pc[self] = "REL_dec"
                 /\ Assert(~freed /\ rc > 0, 
                           "Failure of assertion at line 110, column 5.")
                 /\ rc' = rc - 1
                 /\ last' = [last EXCEPT ![self] = (rc' = 0)]
                 /\ pc' = [pc EXCEPT ![self] = "REL_free"]
                 /\ UNCHANGED << allocated, freed, frees, tmtx, joined, 
                                 detached, tstate, hasThread, thrState, 
                                 thrDetached, thrJoined, published, joinOk, 
                                 errors, stack, caller, claimed >>

REL_free(self) == /\ pc[self] = "REL_free"
                  /\ IF last[self]
                        THEN /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "task_free",
                                                                      pc        |->  "REL_ret" ] >>
                                                                  \o stack[self]]
                             /\ pc' = [pc EXCEPT ![self] = "TF_detach"]
                        ELSE /\ pc' = [pc EXCEPT ![self] = "REL_ret"]
                             /\ stack' = stack
                  /\ UNCHANGED << allocated, freed, frees, rc, tmtx, joined, 
                                  detached, tstate, hasThread, thrState, 
                                  thrDetached, thrJoined, published, joinOk, 
                                  errors, last, caller, claimed >>

REL_ret(self) == /\ pc[self] = "REL_ret"
                 /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
                 /\ last' = [last EXCEPT ![self] = Head(stack[self]).last]
                 /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
                 /\ UNCHANGED << allocated, freed, frees, rc, tmtx, joined, 
                                 detached, tstate, hasThread, thrState, 
                                 thrDetached, thrJoined, published, joinOk, 
                                 errors, caller, claimed >>

release(self) == REL_dec(self) \/ REL_free(self) \/ REL_ret(self)

D_lock(self) == /\ pc[self] = "D_lock"
                /\ Assert(~freed, 
                          "Failure of assertion at line 124, column 5.")
                /\ tmtx = 0
                /\ tmtx' = self
                /\ pc' = [pc EXCEPT ![self] = "D_check"]
                /\ UNCHANGED << allocated, freed, frees, rc, joined, detached, 
                                tstate, hasThread, thrState, thrDetached, 
                                thrJoined, published, joinOk, errors, stack, 
                                last, caller, claimed >>

D_check(self) == /\ pc[self] = "D_check"
                 /\ IF joined
                       THEN /\ tmtx' = 0
                            /\ IF caller[self] \in Holders
                                  THEN /\ errors' = [errors EXCEPT ![caller[self]] = "detach joined"]
                                  ELSE /\ TRUE
                                       /\ UNCHANGED errors
                            /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
                            /\ caller' = [caller EXCEPT ![self] = Head(stack[self]).caller]
                            /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
                       ELSE /\ IF detached
                                  THEN /\ tmtx' = 0
                                       /\ IF caller[self] \in Holders
                                             THEN /\ errors' = [errors EXCEPT ![caller[self]] = "already detached"]
                                             ELSE /\ TRUE
                                                  /\ UNCHANGED errors
                                       /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
                                       /\ caller' = [caller EXCEPT ![self] = Head(stack[self]).caller]
                                       /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
                                  ELSE /\ pc' = [pc EXCEPT ![self] = "D_claim"]
                                       /\ UNCHANGED << tmtx, errors, stack, 
                                                       caller >>
                 /\ UNCHANGED << allocated, freed, frees, rc, joined, detached, 
                                 tstate, hasThread, thrState, thrDetached, 
                                 thrJoined, published, joinOk, last, claimed >>

D_claim(self) == /\ pc[self] = "D_claim"
                 /\ detached' = TRUE
                 /\ tmtx' = 0
                 /\ pc' = [pc EXCEPT ![self] = "D_pthread"]
                 /\ UNCHANGED << allocated, freed, frees, rc, joined, tstate, 
                                 hasThread, thrState, thrDetached, thrJoined, 
                                 published, joinOk, errors, stack, last, 
                                 caller, claimed >>

D_pthread(self) == /\ pc[self] = "D_pthread"
                   /\ Assert(~freed, 
                             "Failure of assertion at line 140, column 5.")
                   /\ Assert(~thrDetached /\ ~thrJoined, 
                             "Failure of assertion at line 84, column 5 of macro called at line 141, column 5.")
                   /\ thrDetached' = TRUE
                   /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
                   /\ caller' = [caller EXCEPT ![self] = Head(stack[self]).caller]
                   /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
                   /\ UNCHANGED << allocated, freed, frees, rc, tmtx, joined, 
                                   detached, tstate, hasThread, thrState, 
                                   thrJoined, published, joinOk, errors, last, 
                                   claimed >>

detach_op(self) == D_lock(self) \/ D_check(self) \/ D_claim(self)
                      \/ D_pthread(self)

SP_alloc == /\ pc[Spawner] = "SP_alloc"
            /\ allocated' = TRUE
            /\ rc' = 2
            /\ IF SpawnDetached /\ Backend = "interp"
                  THEN /\ detached' = TRUE
                  ELSE /\ TRUE
                       /\ UNCHANGED detached
            /\ pc' = [pc EXCEPT ![Spawner] = "SP_create"]
            /\ UNCHANGED << freed, frees, tmtx, joined, tstate, hasThread, 
                            thrState, thrDetached, thrJoined, published, 
                            joinOk, errors, stack, last, caller, claimed >>

SP_create == /\ pc[Spawner] = "SP_create"
             /\ IF CreateFails
                   THEN /\ IF Backend = "compiled"
                              THEN /\ detached' = TRUE
                                   /\ rc' = 1
                              ELSE /\ rc' = 1
                                   /\ UNCHANGED detached
                        /\ stack' = [stack EXCEPT ![Spawner] = << [ procedure |->  "release",
                                                                    pc        |->  "SP_done",
                                                                    last      |->  last[Spawner] ] >>
                                                                \o stack[Spawner]]
                        /\ last' = [last EXCEPT ![Spawner] = FALSE]
                        /\ pc' = [pc EXCEPT ![Spawner] = "REL_dec"]
                        /\ UNCHANGED << hasThread, thrState, thrDetached >>
                   ELSE /\ hasThread' = TRUE
                        /\ thrState' = "running"
                        /\ IF SpawnDetached /\ Backend = "interp"
                              THEN /\ thrDetached' = TRUE
                              ELSE /\ TRUE
                                   /\ UNCHANGED thrDetached
                        /\ pc' = [pc EXCEPT ![Spawner] = "SP_detach_fn"]
                        /\ UNCHANGED << rc, detached, stack, last >>
             /\ UNCHANGED << allocated, freed, frees, tmtx, joined, tstate, 
                             thrJoined, published, joinOk, errors, caller, 
                             claimed >>

SP_detach_fn == /\ pc[Spawner] = "SP_detach_fn"
                /\ IF SpawnDetached
                      THEN /\ IF Backend = "compiled"
                                 THEN /\ /\ caller' = [caller EXCEPT ![Spawner] = Spawner]
                                         /\ stack' = [stack EXCEPT ![Spawner] = << [ procedure |->  "detach_op",
                                                                                     pc        |->  "SP_drop_detached",
                                                                                     caller    |->  caller[Spawner] ] >>
                                                                                 \o stack[Spawner]]
                                      /\ pc' = [pc EXCEPT ![Spawner] = "D_lock"]
                                 ELSE /\ pc' = [pc EXCEPT ![Spawner] = "SP_drop_detached"]
                                      /\ UNCHANGED << stack, caller >>
                      ELSE /\ pc' = [pc EXCEPT ![Spawner] = "SP_publish"]
                           /\ UNCHANGED << stack, caller >>
                /\ UNCHANGED << allocated, freed, frees, rc, tmtx, joined, 
                                detached, tstate, hasThread, thrState, 
                                thrDetached, thrJoined, published, joinOk, 
                                errors, last, claimed >>

SP_drop_detached == /\ pc[Spawner] = "SP_drop_detached"
                    /\ stack' = [stack EXCEPT ![Spawner] = << [ procedure |->  "release",
                                                                pc        |->  "SP_done",
                                                                last      |->  last[Spawner] ] >>
                                                            \o stack[Spawner]]
                    /\ last' = [last EXCEPT ![Spawner] = FALSE]
                    /\ pc' = [pc EXCEPT ![Spawner] = "REL_dec"]
                    /\ UNCHANGED << allocated, freed, frees, rc, tmtx, joined, 
                                    detached, tstate, hasThread, thrState, 
                                    thrDetached, thrJoined, published, joinOk, 
                                    errors, caller, claimed >>

SP_publish == /\ pc[Spawner] = "SP_publish"
              /\ IF NHolders = 0
                    THEN /\ stack' = [stack EXCEPT ![Spawner] = << [ procedure |->  "release",
                                                                     pc        |->  "SP_published",
                                                                     last      |->  last[Spawner] ] >>
                                                                 \o stack[Spawner]]
                         /\ last' = [last EXCEPT ![Spawner] = FALSE]
                         /\ pc' = [pc EXCEPT ![Spawner] = "REL_dec"]
                         /\ rc' = rc
                    ELSE /\ rc' = rc + (NHolders - 1)
                         /\ pc' = [pc EXCEPT ![Spawner] = "SP_published"]
                         /\ UNCHANGED << stack, last >>
              /\ UNCHANGED << allocated, freed, frees, tmtx, joined, detached, 
                              tstate, hasThread, thrState, thrDetached, 
                              thrJoined, published, joinOk, errors, caller, 
                              claimed >>

SP_published == /\ pc[Spawner] = "SP_published"
                /\ published' = TRUE
                /\ pc' = [pc EXCEPT ![Spawner] = "SP_done"]
                /\ UNCHANGED << allocated, freed, frees, rc, tmtx, joined, 
                                detached, tstate, hasThread, thrState, 
                                thrDetached, thrJoined, joinOk, errors, stack, 
                                last, caller, claimed >>

SP_done == /\ pc[Spawner] = "SP_done"
           /\ TRUE
           /\ pc' = [pc EXCEPT ![Spawner] = "Done"]
           /\ UNCHANGED << allocated, freed, frees, rc, tmtx, joined, detached, 
                           tstate, hasThread, thrState, thrDetached, thrJoined, 
                           published, joinOk, errors, stack, last, caller, 
                           claimed >>

spawner == SP_alloc \/ SP_create \/ SP_detach_fn \/ SP_drop_detached
              \/ SP_publish \/ SP_published \/ SP_done

W_start == /\ pc[Worker] = "W_start"
           /\ thrState = "running" \/ pc[Spawner] = "Done"
           /\ IF thrState = "none"
                 THEN /\ pc' = [pc EXCEPT ![Worker] = "W_end"]
                 ELSE /\ pc' = [pc EXCEPT ![Worker] = "W_running"]
           /\ UNCHANGED << allocated, freed, frees, rc, tmtx, joined, detached, 
                           tstate, hasThread, thrState, thrDetached, thrJoined, 
                           published, joinOk, errors, stack, last, caller, 
                           claimed >>

W_running == /\ pc[Worker] = "W_running"
             /\ Assert(~freed, "Failure of assertion at line 206, column 5.")
             /\ tmtx = 0
             /\ tstate' = "running"
             /\ pc' = [pc EXCEPT ![Worker] = "W_complete"]
             /\ UNCHANGED << allocated, freed, frees, rc, tmtx, joined, 
                             detached, hasThread, thrState, thrDetached, 
                             thrJoined, published, joinOk, errors, stack, last, 
                             caller, claimed >>

W_complete == /\ pc[Worker] = "W_complete"
              /\ Assert(~freed, 
                        "Failure of assertion at line 211, column 5.")
              /\ tmtx = 0
              /\ tstate' = "completed"
              /\ pc' = [pc EXCEPT ![Worker] = "W_release"]
              /\ UNCHANGED << allocated, freed, frees, rc, tmtx, joined, 
                              detached, hasThread, thrState, thrDetached, 
                              thrJoined, published, joinOk, errors, stack, 
                              last, caller, claimed >>

W_release == /\ pc[Worker] = "W_release"
             /\ stack' = [stack EXCEPT ![Worker] = << [ procedure |->  "release",
                                                        pc        |->  "W_exit",
                                                        last      |->  last[Worker] ] >>
                                                    \o stack[Worker]]
             /\ last' = [last EXCEPT ![Worker] = FALSE]
             /\ pc' = [pc EXCEPT ![Worker] = "REL_dec"]
             /\ UNCHANGED << allocated, freed, frees, rc, tmtx, joined, 
                             detached, tstate, hasThread, thrState, 
                             thrDetached, thrJoined, published, joinOk, errors, 
                             caller, claimed >>

W_exit == /\ pc[Worker] = "W_exit"
          /\ thrState' = "exited"
          /\ pc' = [pc EXCEPT ![Worker] = "W_end"]
          /\ UNCHANGED << allocated, freed, frees, rc, tmtx, joined, detached, 
                          tstate, hasThread, thrDetached, thrJoined, published, 
                          joinOk, errors, stack, last, caller, claimed >>

W_end == /\ pc[Worker] = "W_end"
         /\ TRUE
         /\ pc' = [pc EXCEPT ![Worker] = "Done"]
         /\ UNCHANGED << allocated, freed, frees, rc, tmtx, joined, detached, 
                         tstate, hasThread, thrState, thrDetached, thrJoined, 
                         published, joinOk, errors, stack, last, caller, 
                         claimed >>

worker == W_start \/ W_running \/ W_complete \/ W_release \/ W_exit
             \/ W_end

H_start(self) == /\ pc[self] = "H_start"
                 /\ published
                 /\ \/ /\ pc' = [pc EXCEPT ![self] = "H_join_lock"]
                       /\ UNCHANGED <<stack, caller>>
                    \/ /\ /\ caller' = [caller EXCEPT ![self] = self]
                          /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "detach_op",
                                                                   pc        |->  "H_drop",
                                                                   caller    |->  caller[self] ] >>
                                                               \o stack[self]]
                       /\ pc' = [pc EXCEPT ![self] = "D_lock"]
                    \/ /\ TRUE
                       /\ pc' = [pc EXCEPT ![self] = "H_drop"]
                       /\ UNCHANGED <<stack, caller>>
                 /\ UNCHANGED << allocated, freed, frees, rc, tmtx, joined, 
                                 detached, tstate, hasThread, thrState, 
                                 thrDetached, thrJoined, published, joinOk, 
                                 errors, last, claimed >>

H_join_lock(self) == /\ pc[self] = "H_join_lock"
                     /\ Assert(~freed, 
                               "Failure of assertion at line 230, column 9.")
                     /\ tmtx = 0
                     /\ tmtx' = self
                     /\ pc' = [pc EXCEPT ![self] = "H_join_check"]
                     /\ UNCHANGED << allocated, freed, frees, rc, joined, 
                                     detached, tstate, hasThread, thrState, 
                                     thrDetached, thrJoined, published, joinOk, 
                                     errors, stack, last, caller, claimed >>

H_join_check(self) == /\ pc[self] = "H_join_check"
                      /\ IF joined
                            THEN /\ errors' = [errors EXCEPT ![self] = "already joined"]
                                 /\ UNCHANGED << joined, claimed >>
                            ELSE /\ IF detached
                                       THEN /\ errors' = [errors EXCEPT ![self] = "join detached"]
                                            /\ UNCHANGED << joined, claimed >>
                                       ELSE /\ joined' = TRUE
                                            /\ claimed' = [claimed EXCEPT ![self] = TRUE]
                                            /\ UNCHANGED errors
                      /\ tmtx' = 0
                      /\ pc' = [pc EXCEPT ![self] = "H_pthread_join"]
                      /\ UNCHANGED << allocated, freed, frees, rc, detached, 
                                      tstate, hasThread, thrState, thrDetached, 
                                      thrJoined, published, joinOk, stack, 
                                      last, caller >>

H_pthread_join(self) == /\ pc[self] = "H_pthread_join"
                        /\ IF claimed[self]
                              THEN /\ Assert(~freed, 
                                             "Failure of assertion at line 243, column 13.")
                                   /\ Assert(~thrDetached /\ ~thrJoined, 
                                             "Failure of assertion at line 244, column 13.")
                                   /\ thrState = "exited"
                                   /\ thrJoined' = TRUE
                                   /\ pc' = [pc EXCEPT ![self] = "H_read_result"]
                              ELSE /\ pc' = [pc EXCEPT ![self] = "H_drop"]
                                   /\ UNCHANGED thrJoined
                        /\ UNCHANGED << allocated, freed, frees, rc, tmtx, 
                                        joined, detached, tstate, hasThread, 
                                        thrState, thrDetached, published, 
                                        joinOk, errors, stack, last, caller, 
                                        claimed >>

H_read_result(self) == /\ pc[self] = "H_read_result"
                       /\ Assert(~freed /\ tstate = "completed", 
                                 "Failure of assertion at line 249, column 13.")
                       /\ joinOk' = (joinOk \cup {self})
                       /\ pc' = [pc EXCEPT ![self] = "H_drop"]
                       /\ UNCHANGED << allocated, freed, frees, rc, tmtx, 
                                       joined, detached, tstate, hasThread, 
                                       thrState, thrDetached, thrJoined, 
                                       published, errors, stack, last, caller, 
                                       claimed >>

H_drop(self) == /\ pc[self] = "H_drop"
                /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "release",
                                                         pc        |->  "Done",
                                                         last      |->  last[self] ] >>
                                                     \o stack[self]]
                /\ last' = [last EXCEPT ![self] = FALSE]
                /\ pc' = [pc EXCEPT ![self] = "REL_dec"]
                /\ UNCHANGED << allocated, freed, frees, rc, tmtx, joined, 
                                detached, tstate, hasThread, thrState, 
                                thrDetached, thrJoined, published, joinOk, 
                                errors, caller, claimed >>

holder(self) == H_start(self) \/ H_join_lock(self) \/ H_join_check(self)
                   \/ H_pthread_join(self) \/ H_read_result(self)
                   \/ H_drop(self)

(* Allow infinite stuttering to prevent deadlock on termination. *)
Terminating == /\ \A self \in ProcSet: pc[self] = "Done"
               /\ UNCHANGED vars

Next == spawner \/ worker
           \/ (\E self \in ProcSet:  \/ task_free(self) \/ release(self)
                                     \/ detach_op(self))
           \/ (\E self \in Holders: holder(self))
           \/ Terminating

Spec == Init /\ [][Next]_vars

Termination == <>(\A self \in ProcSet: pc[self] = "Done")

\* END TRANSLATION
==============================================================================
