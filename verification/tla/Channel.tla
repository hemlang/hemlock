------------------------------- MODULE Channel -------------------------------
(***************************************************************************)
(* Model of the channel core in src/shared/chan_core.c: hml_chan_send,     *)
(* hml_chan_recv and hml_chan_close, transcribed step for step.            *)
(*                                                                         *)
(* pthread condition variables are modelled with POSIX's weakest           *)
(* guarantees:                                                             *)
(*   - wait atomically releases the mutex and parks the thread;            *)
(*   - signal unparks at least one parked thread (we pick any one);        *)
(*   - broadcast unparks all of them;                                      *)
(*   - with Spurious, a parked thread may wake for no reason;              *)
(*   - with Timeouts, a timed wait may return ETIMEDOUT at any point,      *)
(*     INCLUDING after it was signalled (POSIX lets a timed-out waiter     *)
(*     consume a signal), which is the classic lost-wakeup hazard;         *)
(*   - on return the mutex is re-acquired.                                 *)
(*                                                                         *)
(* Sender s sends the item s, so items are unique and traceable.           *)
(* select() is not modelled here.                                          *)
(***************************************************************************)
EXTENDS Naturals, Sequences, FiniteSets, TLC

CONSTANTS Cap,        \* user-visible capacity (0 = rendezvous)
          NSenders, NReceivers,
          Timeouts,   \* timed waits may time out
          Spurious,   \* condvar waits may wake spuriously
          WithClose   \* a closer thread runs concurrently

Senders   == 1..NSenders
Receivers == 101..(100 + NReceivers)
Closer    == 200
SlotCap   == IF Cap > 0 THEN Cap ELSE 1
Items     == Senders

(* --algorithm channel
variables
    lock = 0,                          \* 0 = unlocked, else holder pid
    buf = <<>>,                        \* the ring, head first
    closed = FALSE,
    recvGen = 0,
    parked = [c \in {"ne", "nf"} |-> {}],
    enqueued = <<>>,                   \* history: items in enqueue order
    received = <<>>,                   \* history: items in receive order
    released = {},                     \* items the core released (dropped)
    sendRes = [s \in Senders |-> "none"],
    recvRes = [r \in Receivers |-> "none"];

define
    Count == Len(buf)
    Range(f) == {f[i] : i \in DOMAIN f}

    \* --- safety ---
    TypeOK == /\ Count <= SlotCap
              /\ lock \in {0} \cup Senders \cup Receivers \cup {Closer}
    \* No item is delivered twice.
    NoDuplicateDelivery == \A i, j \in DOMAIN received : i # j => received[i] # received[j]
    \* An item is never both delivered and dropped.
    DeliveredXorReleased == Range(received) \cap released = {}
    \* Delivery order is enqueue order with withdrawn items removed (FIFO).
    FIFO == LET kept == SelectSeq(enqueued, LAMBDA x : x \notin released)
            IN received = SubSeq(kept, 1, Len(received))
    \* Send's status tells the truth about its item.
    SendContract == \A s \in Senders :
        /\ sendRes[s] = "ok" =>
             IF Cap = 0 THEN s \in Range(received)        \* rendezvous: handed off
             ELSE s \in Range(received) \cup Range(buf)   \* buffered: queued
        /\ sendRes[s] \in {"timeout", "closed"} =>
             s \in released /\ s \notin Range(received)
    \* Ownership: once every thread is done, every item is accounted for exactly
    \* once (delivered, released by the core, or still queued for chan_free).
    Conservation == (\A p \in Senders \cup Receivers : pc[p] = "Done") =>
        \A s \in Items : Cardinality({k \in {"recv", "rel", "buf"} :
            \/ (k = "recv" /\ s \in Range(received))
            \/ (k = "rel"  /\ s \in released)
            \/ (k = "buf"  /\ s \in Range(buf))}) = 1
    \* recv reports CLOSED only when the channel is closed and drained.
    RecvClosedOnlyWhenDrained == \A r \in Receivers :
        recvRes[r] = "closed" => closed
end define;

macro signal(c) begin
    if parked[c] # {} then
        with w \in parked[c] do parked[c] := parked[c] \ {w}; end with;
    end if;
end macro;

macro broadcast(c) begin
    parked[c] := {};
end macro;

\* wake_senders(): broadcast for rendezvous, signal otherwise.
macro wake_senders() begin
    if Cap = 0 then broadcast("nf"); else signal("nf"); end if;
end macro;

\* wait_cond(): sets timedOut. Re-acquires the mutex before returning.
procedure wait(cv) begin
  W_park:
    parked[cv] := parked[cv] \cup {self};
    lock := 0;
  W_wake:
    either
        await self \notin parked[cv];          \* signalled / broadcast
        timedOut := FALSE;
    or
        await Timeouts;                        \* ETIMEDOUT (maybe after a signal)
        parked[cv] := parked[cv] \ {self};
        timedOut := TRUE;
    or
        await Spurious /\ self \in parked[cv]; \* spurious wakeup
        parked[cv] := parked[cv] \ {self};
        timedOut := FALSE;
    end either;
  W_relock:
    await lock = 0;
    lock := self;
    return;
end procedure;

process sender \in Senders
variables timedOut = FALSE, staged = 0;
begin
  S_lock:
    await lock = 0; lock := self;
  S_phase1:
    while Count = SlotCap /\ ~closed do
        call wait("nf");
  S_phase1_check:
        if timedOut /\ Count = SlotCap /\ ~closed then
            lock := 0;
            released := released \cup {self};
            sendRes[self] := "timeout";
            goto S_end;
        end if;
    end while;
  S_closed_check:
    if closed then
        lock := 0;
        released := released \cup {self};
        sendRes[self] := "closed";
        goto S_end;
    end if;
  S_enqueue:
    buf := Append(buf, self);
    enqueued := Append(enqueued, self);
    signal("ne");
    if Cap > 0 then
        lock := 0;
        sendRes[self] := "ok";
        goto S_end;
    end if;
  S_stage:
    staged := recvGen;
  S_phase2:
    while recvGen = staged /\ ~closed do
        call wait("nf");
  S_phase2_check:
        if timedOut /\ recvGen = staged then
            \* withdraw_staged(): drop slot 0 without counting a receive
            released := released \cup {Head(buf)};
            buf := <<>>;
            wake_senders();
            lock := 0;
            sendRes[self] := "timeout";
            goto S_end;
        end if;
    end while;
  S_handoff_check:
    if recvGen = staged then
        released := released \cup {Head(buf)};
        buf := <<>>;
        lock := 0;
        sendRes[self] := "closed";
        goto S_end;
    end if;
  S_ok:
    lock := 0;
    sendRes[self] := "ok";
  S_end:
    skip;
end process;

process receiver \in Receivers
variables timedOut = FALSE;
begin
  R_lock:
    await lock = 0; lock := self;
  R_wait:
    while Count = 0 /\ ~closed do
        call wait("ne");
  R_wait_check:
        if timedOut /\ Count = 0 /\ ~closed then
            lock := 0;
            recvRes[self] := "timeout";
            goto R_end;
        end if;
    end while;
  R_take:
    if Count = 0 then
        lock := 0;
        recvRes[self] := "closed";
        goto R_end;
    end if;
  R_dequeue:
    received := Append(received, Head(buf));
    buf := Tail(buf);
    recvGen := recvGen + 1;
    wake_senders();
    lock := 0;
    recvRes[self] := "ok";
  R_end:
    skip;
end process;

process closer = Closer
begin
  C_lock:
    if WithClose then
        await lock = 0; lock := Closer;
  C_close:
        closed := TRUE;
        parked := [c \in {"ne", "nf"} |-> {}];   \* broadcast both condvars
        lock := 0;
    end if;
end process;

end algorithm; *)
\* BEGIN TRANSLATION (chksum(pcal) = "93a5530d" /\ chksum(tla) = "65a7dddc")
\* Process variable timedOut of process sender at line 120 col 11 changed to timedOut_
CONSTANT defaultInitValue
VARIABLES pc, lock, buf, closed, recvGen, parked, enqueued, received, 
          released, sendRes, recvRes, stack

(* define statement *)
Count == Len(buf)
Range(f) == {f[i] : i \in DOMAIN f}


TypeOK == /\ Count <= SlotCap
          /\ lock \in {0} \cup Senders \cup Receivers \cup {Closer}

NoDuplicateDelivery == \A i, j \in DOMAIN received : i # j => received[i] # received[j]

DeliveredXorReleased == Range(received) \cap released = {}

FIFO == LET kept == SelectSeq(enqueued, LAMBDA x : x \notin released)
        IN received = SubSeq(kept, 1, Len(received))

SendContract == \A s \in Senders :
    /\ sendRes[s] = "ok" =>
         IF Cap = 0 THEN s \in Range(received)
         ELSE s \in Range(received) \cup Range(buf)
    /\ sendRes[s] \in {"timeout", "closed"} =>
         s \in released /\ s \notin Range(received)


Conservation == (\A p \in Senders \cup Receivers : pc[p] = "Done") =>
    \A s \in Items : Cardinality({k \in {"recv", "rel", "buf"} :
        \/ (k = "recv" /\ s \in Range(received))
        \/ (k = "rel"  /\ s \in released)
        \/ (k = "buf"  /\ s \in Range(buf))}) = 1

RecvClosedOnlyWhenDrained == \A r \in Receivers :
    recvRes[r] = "closed" => closed

VARIABLES cv, timedOut_, staged, timedOut

vars == << pc, lock, buf, closed, recvGen, parked, enqueued, received, 
           released, sendRes, recvRes, stack, cv, timedOut_, staged, timedOut
        >>

ProcSet == (Senders) \cup (Receivers) \cup {Closer}

Init == (* Global variables *)
        /\ lock = 0
        /\ buf = <<>>
        /\ closed = FALSE
        /\ recvGen = 0
        /\ parked = [c \in {"ne", "nf"} |-> {}]
        /\ enqueued = <<>>
        /\ received = <<>>
        /\ released = {}
        /\ sendRes = [s \in Senders |-> "none"]
        /\ recvRes = [r \in Receivers |-> "none"]
        (* Procedure wait *)
        /\ cv = [ self \in ProcSet |-> defaultInitValue]
        (* Process sender *)
        /\ timedOut_ = [self \in Senders |-> FALSE]
        /\ staged = [self \in Senders |-> 0]
        (* Process receiver *)
        /\ timedOut = [self \in Receivers |-> FALSE]
        /\ stack = [self \in ProcSet |-> << >>]
        /\ pc = [self \in ProcSet |-> CASE self \in Senders -> "S_lock"
                                        [] self \in Receivers -> "R_lock"
                                        [] self = Closer -> "C_lock"]

W_park(self) == /\ pc[self] = "W_park"
                /\ parked' = [parked EXCEPT ![cv[self]] = parked[cv[self]] \cup {self}]
                /\ lock' = 0
                /\ pc' = [pc EXCEPT ![self] = "W_wake"]
                /\ UNCHANGED << buf, closed, recvGen, enqueued, received, 
                                released, sendRes, recvRes, stack, cv, 
                                timedOut_, staged, timedOut >>

W_wake(self) == /\ pc[self] = "W_wake"
                /\ \/ /\ self \notin parked[cv[self]]
                      /\ timedOut' = [timedOut EXCEPT ![self] = FALSE]
                      /\ UNCHANGED parked
                   \/ /\ Timeouts
                      /\ parked' = [parked EXCEPT ![cv[self]] = parked[cv[self]] \ {self}]
                      /\ timedOut' = [timedOut EXCEPT ![self] = TRUE]
                   \/ /\ Spurious /\ self \in parked[cv[self]]
                      /\ parked' = [parked EXCEPT ![cv[self]] = parked[cv[self]] \ {self}]
                      /\ timedOut' = [timedOut EXCEPT ![self] = FALSE]
                /\ pc' = [pc EXCEPT ![self] = "W_relock"]
                /\ UNCHANGED << lock, buf, closed, recvGen, enqueued, received, 
                                released, sendRes, recvRes, stack, cv, 
                                timedOut_, staged >>

W_relock(self) == /\ pc[self] = "W_relock"
                  /\ lock = 0
                  /\ lock' = self
                  /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
                  /\ cv' = [cv EXCEPT ![self] = Head(stack[self]).cv]
                  /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
                  /\ UNCHANGED << buf, closed, recvGen, parked, enqueued, 
                                  received, released, sendRes, recvRes, 
                                  timedOut_, staged, timedOut >>

wait(self) == W_park(self) \/ W_wake(self) \/ W_relock(self)

S_lock(self) == /\ pc[self] = "S_lock"
                /\ lock = 0
                /\ lock' = self
                /\ pc' = [pc EXCEPT ![self] = "S_phase1"]
                /\ UNCHANGED << buf, closed, recvGen, parked, enqueued, 
                                received, released, sendRes, recvRes, stack, 
                                cv, timedOut_, staged, timedOut >>

S_phase1(self) == /\ pc[self] = "S_phase1"
                  /\ IF Count = SlotCap /\ ~closed
                        THEN /\ /\ cv' = [cv EXCEPT ![self] = "nf"]
                                /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "wait",
                                                                         pc        |->  "S_phase1_check",
                                                                         cv        |->  cv[self] ] >>
                                                                     \o stack[self]]
                             /\ pc' = [pc EXCEPT ![self] = "W_park"]
                        ELSE /\ pc' = [pc EXCEPT ![self] = "S_closed_check"]
                             /\ UNCHANGED << stack, cv >>
                  /\ UNCHANGED << lock, buf, closed, recvGen, parked, enqueued, 
                                  received, released, sendRes, recvRes, 
                                  timedOut_, staged, timedOut >>

S_phase1_check(self) == /\ pc[self] = "S_phase1_check"
                        /\ IF timedOut_[self] /\ Count = SlotCap /\ ~closed
                              THEN /\ lock' = 0
                                   /\ released' = (released \cup {self})
                                   /\ sendRes' = [sendRes EXCEPT ![self] = "timeout"]
                                   /\ pc' = [pc EXCEPT ![self] = "S_end"]
                              ELSE /\ pc' = [pc EXCEPT ![self] = "S_phase1"]
                                   /\ UNCHANGED << lock, released, sendRes >>
                        /\ UNCHANGED << buf, closed, recvGen, parked, enqueued, 
                                        received, recvRes, stack, cv, 
                                        timedOut_, staged, timedOut >>

S_closed_check(self) == /\ pc[self] = "S_closed_check"
                        /\ IF closed
                              THEN /\ lock' = 0
                                   /\ released' = (released \cup {self})
                                   /\ sendRes' = [sendRes EXCEPT ![self] = "closed"]
                                   /\ pc' = [pc EXCEPT ![self] = "S_end"]
                              ELSE /\ pc' = [pc EXCEPT ![self] = "S_enqueue"]
                                   /\ UNCHANGED << lock, released, sendRes >>
                        /\ UNCHANGED << buf, closed, recvGen, parked, enqueued, 
                                        received, recvRes, stack, cv, 
                                        timedOut_, staged, timedOut >>

S_enqueue(self) == /\ pc[self] = "S_enqueue"
                   /\ buf' = Append(buf, self)
                   /\ enqueued' = Append(enqueued, self)
                   /\ IF parked["ne"] # {}
                         THEN /\ \E w \in parked["ne"]:
                                   parked' = [parked EXCEPT !["ne"] = parked["ne"] \ {w}]
                         ELSE /\ TRUE
                              /\ UNCHANGED parked
                   /\ IF Cap > 0
                         THEN /\ lock' = 0
                              /\ sendRes' = [sendRes EXCEPT ![self] = "ok"]
                              /\ pc' = [pc EXCEPT ![self] = "S_end"]
                         ELSE /\ pc' = [pc EXCEPT ![self] = "S_stage"]
                              /\ UNCHANGED << lock, sendRes >>
                   /\ UNCHANGED << closed, recvGen, received, released, 
                                   recvRes, stack, cv, timedOut_, staged, 
                                   timedOut >>

S_stage(self) == /\ pc[self] = "S_stage"
                 /\ staged' = [staged EXCEPT ![self] = recvGen]
                 /\ pc' = [pc EXCEPT ![self] = "S_phase2"]
                 /\ UNCHANGED << lock, buf, closed, recvGen, parked, enqueued, 
                                 received, released, sendRes, recvRes, stack, 
                                 cv, timedOut_, timedOut >>

S_phase2(self) == /\ pc[self] = "S_phase2"
                  /\ IF recvGen = staged[self] /\ ~closed
                        THEN /\ /\ cv' = [cv EXCEPT ![self] = "nf"]
                                /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "wait",
                                                                         pc        |->  "S_phase2_check",
                                                                         cv        |->  cv[self] ] >>
                                                                     \o stack[self]]
                             /\ pc' = [pc EXCEPT ![self] = "W_park"]
                        ELSE /\ pc' = [pc EXCEPT ![self] = "S_handoff_check"]
                             /\ UNCHANGED << stack, cv >>
                  /\ UNCHANGED << lock, buf, closed, recvGen, parked, enqueued, 
                                  received, released, sendRes, recvRes, 
                                  timedOut_, staged, timedOut >>

S_phase2_check(self) == /\ pc[self] = "S_phase2_check"
                        /\ IF timedOut_[self] /\ recvGen = staged[self]
                              THEN /\ released' = (released \cup {Head(buf)})
                                   /\ buf' = <<>>
                                   /\ IF Cap = 0
                                         THEN /\ parked' = [parked EXCEPT !["nf"] = {}]
                                         ELSE /\ IF parked["nf"] # {}
                                                    THEN /\ \E w \in parked["nf"]:
                                                              parked' = [parked EXCEPT !["nf"] = parked["nf"] \ {w}]
                                                    ELSE /\ TRUE
                                                         /\ UNCHANGED parked
                                   /\ lock' = 0
                                   /\ sendRes' = [sendRes EXCEPT ![self] = "timeout"]
                                   /\ pc' = [pc EXCEPT ![self] = "S_end"]
                              ELSE /\ pc' = [pc EXCEPT ![self] = "S_phase2"]
                                   /\ UNCHANGED << lock, buf, parked, released, 
                                                   sendRes >>
                        /\ UNCHANGED << closed, recvGen, enqueued, received, 
                                        recvRes, stack, cv, timedOut_, staged, 
                                        timedOut >>

S_handoff_check(self) == /\ pc[self] = "S_handoff_check"
                         /\ IF recvGen = staged[self]
                               THEN /\ released' = (released \cup {Head(buf)})
                                    /\ buf' = <<>>
                                    /\ lock' = 0
                                    /\ sendRes' = [sendRes EXCEPT ![self] = "closed"]
                                    /\ pc' = [pc EXCEPT ![self] = "S_end"]
                               ELSE /\ pc' = [pc EXCEPT ![self] = "S_ok"]
                                    /\ UNCHANGED << lock, buf, released, 
                                                    sendRes >>
                         /\ UNCHANGED << closed, recvGen, parked, enqueued, 
                                         received, recvRes, stack, cv, 
                                         timedOut_, staged, timedOut >>

S_ok(self) == /\ pc[self] = "S_ok"
              /\ lock' = 0
              /\ sendRes' = [sendRes EXCEPT ![self] = "ok"]
              /\ pc' = [pc EXCEPT ![self] = "S_end"]
              /\ UNCHANGED << buf, closed, recvGen, parked, enqueued, received, 
                              released, recvRes, stack, cv, timedOut_, staged, 
                              timedOut >>

S_end(self) == /\ pc[self] = "S_end"
               /\ TRUE
               /\ pc' = [pc EXCEPT ![self] = "Done"]
               /\ UNCHANGED << lock, buf, closed, recvGen, parked, enqueued, 
                               received, released, sendRes, recvRes, stack, cv, 
                               timedOut_, staged, timedOut >>

sender(self) == S_lock(self) \/ S_phase1(self) \/ S_phase1_check(self)
                   \/ S_closed_check(self) \/ S_enqueue(self)
                   \/ S_stage(self) \/ S_phase2(self)
                   \/ S_phase2_check(self) \/ S_handoff_check(self)
                   \/ S_ok(self) \/ S_end(self)

R_lock(self) == /\ pc[self] = "R_lock"
                /\ lock = 0
                /\ lock' = self
                /\ pc' = [pc EXCEPT ![self] = "R_wait"]
                /\ UNCHANGED << buf, closed, recvGen, parked, enqueued, 
                                received, released, sendRes, recvRes, stack, 
                                cv, timedOut_, staged, timedOut >>

R_wait(self) == /\ pc[self] = "R_wait"
                /\ IF Count = 0 /\ ~closed
                      THEN /\ /\ cv' = [cv EXCEPT ![self] = "ne"]
                              /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "wait",
                                                                       pc        |->  "R_wait_check",
                                                                       cv        |->  cv[self] ] >>
                                                                   \o stack[self]]
                           /\ pc' = [pc EXCEPT ![self] = "W_park"]
                      ELSE /\ pc' = [pc EXCEPT ![self] = "R_take"]
                           /\ UNCHANGED << stack, cv >>
                /\ UNCHANGED << lock, buf, closed, recvGen, parked, enqueued, 
                                received, released, sendRes, recvRes, 
                                timedOut_, staged, timedOut >>

R_wait_check(self) == /\ pc[self] = "R_wait_check"
                      /\ IF timedOut[self] /\ Count = 0 /\ ~closed
                            THEN /\ lock' = 0
                                 /\ recvRes' = [recvRes EXCEPT ![self] = "timeout"]
                                 /\ pc' = [pc EXCEPT ![self] = "R_end"]
                            ELSE /\ pc' = [pc EXCEPT ![self] = "R_wait"]
                                 /\ UNCHANGED << lock, recvRes >>
                      /\ UNCHANGED << buf, closed, recvGen, parked, enqueued, 
                                      received, released, sendRes, stack, cv, 
                                      timedOut_, staged, timedOut >>

R_take(self) == /\ pc[self] = "R_take"
                /\ IF Count = 0
                      THEN /\ lock' = 0
                           /\ recvRes' = [recvRes EXCEPT ![self] = "closed"]
                           /\ pc' = [pc EXCEPT ![self] = "R_end"]
                      ELSE /\ pc' = [pc EXCEPT ![self] = "R_dequeue"]
                           /\ UNCHANGED << lock, recvRes >>
                /\ UNCHANGED << buf, closed, recvGen, parked, enqueued, 
                                received, released, sendRes, stack, cv, 
                                timedOut_, staged, timedOut >>

R_dequeue(self) == /\ pc[self] = "R_dequeue"
                   /\ received' = Append(received, Head(buf))
                   /\ buf' = Tail(buf)
                   /\ recvGen' = recvGen + 1
                   /\ IF Cap = 0
                         THEN /\ parked' = [parked EXCEPT !["nf"] = {}]
                         ELSE /\ IF parked["nf"] # {}
                                    THEN /\ \E w \in parked["nf"]:
                                              parked' = [parked EXCEPT !["nf"] = parked["nf"] \ {w}]
                                    ELSE /\ TRUE
                                         /\ UNCHANGED parked
                   /\ lock' = 0
                   /\ recvRes' = [recvRes EXCEPT ![self] = "ok"]
                   /\ pc' = [pc EXCEPT ![self] = "R_end"]
                   /\ UNCHANGED << closed, enqueued, released, sendRes, stack, 
                                   cv, timedOut_, staged, timedOut >>

R_end(self) == /\ pc[self] = "R_end"
               /\ TRUE
               /\ pc' = [pc EXCEPT ![self] = "Done"]
               /\ UNCHANGED << lock, buf, closed, recvGen, parked, enqueued, 
                               received, released, sendRes, recvRes, stack, cv, 
                               timedOut_, staged, timedOut >>

receiver(self) == R_lock(self) \/ R_wait(self) \/ R_wait_check(self)
                     \/ R_take(self) \/ R_dequeue(self) \/ R_end(self)

C_lock == /\ pc[Closer] = "C_lock"
          /\ IF WithClose
                THEN /\ lock = 0
                     /\ lock' = Closer
                     /\ pc' = [pc EXCEPT ![Closer] = "C_close"]
                ELSE /\ pc' = [pc EXCEPT ![Closer] = "Done"]
                     /\ lock' = lock
          /\ UNCHANGED << buf, closed, recvGen, parked, enqueued, received, 
                          released, sendRes, recvRes, stack, cv, timedOut_, 
                          staged, timedOut >>

C_close == /\ pc[Closer] = "C_close"
           /\ closed' = TRUE
           /\ parked' = [c \in {"ne", "nf"} |-> {}]
           /\ lock' = 0
           /\ pc' = [pc EXCEPT ![Closer] = "Done"]
           /\ UNCHANGED << buf, recvGen, enqueued, received, released, sendRes, 
                           recvRes, stack, cv, timedOut_, staged, timedOut >>

closer == C_lock \/ C_close

(* Allow infinite stuttering to prevent deadlock on termination. *)
Terminating == /\ \A self \in ProcSet: pc[self] = "Done"
               /\ UNCHANGED vars

Next == closer
           \/ (\E self \in ProcSet: wait(self))
           \/ (\E self \in Senders: sender(self))
           \/ (\E self \in Receivers: receiver(self))
           \/ Terminating

Spec == Init /\ [][Next]_vars

Termination == <>(\A self \in ProcSet: pc[self] = "Done")

\* END TRANSLATION 
==============================================================================
