---------------------------- MODULE ChannelSelect ----------------------------
(***************************************************************************)
(* Model of hml_chan_select in src/shared/chan_core.c, running against     *)
(* concurrent hml_chan_send / hml_chan_recv / hml_chan_close on several     *)
(* channels. Channel.tla covers one channel without select(); this module   *)
(* re-states send/recv/close per channel (same step structure) so that     *)
(* select() can be checked against all of them.                            *)
(*                                                                         *)
(* Every mutex is modelled: one per channel and one per select waiter. A   *)
(* step that takes a mutex, works on the state it protects and releases it *)
(* is one atomic step (nothing else can touch that state in between); a    *)
(* mutex held across a blocking point is held explicitly in `mtx`, so a    *)
(* lock-order deadlock shows up as a TLC deadlock.                          *)
(*                                                                         *)
(* Condition variables have POSIX's weakest guarantees, as in Channel.tla: *)
(* signal wakes any one waiter, spurious wakeups (Spurious), and a timed   *)
(* wait may time out even after being signalled (Timeouts).                *)
(*                                                                         *)
(* The select waiter lives on the select() caller's stack: `live` tracks   *)
(* its lifetime, and notifying a dead waiter is an assertion failure.      *)
(* The rotating start index is chosen nondeterministically (it is a global *)
(* atomic counter in the code, so any start is possible).                  *)
(*                                                                         *)
(* Sender s sends the item s, so items are unique and traceable.           *)
(***************************************************************************)
EXTENDS Naturals, Sequences, FiniteSets, TLC

CONSTANTS Caps,        \* Caps[c]: user-visible capacity of channel c (0 = rendezvous)
          SendTo,      \* SendTo[s]: the channel sender s sends on
          RecvOn,      \* RecvOn[i]: the channel plain receiver i receives on
          NSelectors,  \* each selector runs one select() over every channel
          CloseSet,    \* channels the closer closes (in increasing order)
          Timeouts,    \* timed waits may time out
          Spurious     \* condvar waits may wake spuriously

\* Scenarios, bound to the constants with `Caps <- Caps_Pair` etc. in the
\* .cfg files (TLC configs cannot write tuples).
Caps_Pair    == <<0, 1>>     \* a rendezvous and a buffered channel
SendTo_Each  == <<1, 2>>     \* one sender per channel
SendTo_First == <<1, 1>>     \* both senders on channel 1
RecvOn_None  == <<>>
RecvOn_First == <<1>>        \* a plain receiver competing with select() on channel 1

NChans    == Len(Caps)
Chans     == 1..NChans
Senders   == 1..Len(SendTo)
Receivers == 101..(100 + Len(RecvOn))
Selectors == 201..(200 + NSelectors)
Closer    == 300
RecvChan(r) == RecvOn[r - 100]
SlotCap(c)  == IF Caps[c] > 0 THEN Caps[c] ELSE 1
Items     == Senders

\* Mutexes and condition variables.
Mutexes == ({"ch"} \X Chans) \cup ({"w"} \X Selectors)
CVs     == ({"ne", "nf"} \X Chans) \cup ({"w"} \X Selectors)
ChM(c)  == <<"ch", c>>
WM(w)   == <<"w", w>>

(* --algorithm select
variables
    mtx = [m \in Mutexes |-> 0],             \* 0 = unlocked, else holder pid
    parked = [v \in CVs |-> {}],
    buf = [c \in Chans |-> <<>>],
    closed = [c \in Chans |-> FALSE],
    recvGen = [c \in Chans |-> 0],
    regs = [c \in Chans |-> {}],             \* select waiters registered on c
    ready = [w \in Selectors |-> FALSE],     \* SelectWaiter.ready
    live = [w \in Selectors |-> FALSE],      \* SelectWaiter is initialised, not destroyed
    enqueued = [c \in Chans |-> <<>>],       \* history: items in enqueue order
    received = [c \in Chans |-> <<>>],       \* history: items in receive order
    released = {},                           \* items the core released (dropped)
    sendRes = [s \in Senders |-> "none"],
    recvRes = [r \in Receivers |-> "none"],
    selRes = [w \in Selectors |-> "none"],
    selIdx = [w \in Selectors |-> 0],
    selItem = [w \in Selectors |-> 0],
    \* Written by procedures, so global and indexed by thread: PlusCal
    \* procedures cannot see process-local variables.
    timedOut = [p \in Senders \cup Receivers \cup Selectors |-> FALSE],
    found = [w \in Selectors |-> FALSE],
    scanRes = [w \in Selectors |-> "none"];

define
    Count(c) == Len(buf[c])
    Range(f) == {f[i] : i \in DOMAIN f}
    AllReceived == UNION {Range(received[c]) : c \in Chans}
    AllBuffered == UNION {Range(buf[c]) : c \in Chans}
    Workers == Senders \cup Receivers \cup Selectors

    \* --- safety ---
    TypeOK == \A c \in Chans : Count(c) <= SlotCap(c)
    \* No item is delivered twice, on any channel, to receivers or selectors.
    NoDuplicateDelivery ==
        /\ \A c \in Chans : \A i, j \in DOMAIN received[c] :
               i # j => received[c][i] # received[c][j]
        /\ \A c, d \in Chans : c # d => Range(received[c]) \cap Range(received[d]) = {}
    \* An item is never both delivered and dropped.
    DeliveredXorReleased == AllReceived \cap released = {}
    \* Items only come out of the channel they were sent on.
    RightChannel == \A c \in Chans : \A x \in Range(received[c]) : SendTo[x] = c
    \* Per-channel delivery order is enqueue order minus withdrawn items (FIFO),
    \* whether the receiver was recv() or select().
    FIFO == \A c \in Chans :
        LET kept == SelectSeq(enqueued[c], LAMBDA x : x \notin released)
        IN received[c] = SubSeq(kept, 1, Len(received[c]))
    \* Send's status tells the truth about its item.
    SendContract == \A s \in Senders :
        /\ sendRes[s] = "ok" =>
             IF Caps[SendTo[s]] = 0 THEN s \in AllReceived
             ELSE s \in AllReceived \cup AllBuffered
        /\ sendRes[s] \in {"timeout", "closed"} =>
             s \in released /\ s \notin AllReceived
    \* select()'s result is the item it took, from the channel it names;
    \* CLOSED only for a closed channel; nothing is taken on CLOSED/TIMEOUT.
    SelectContract == \A w \in Selectors :
        /\ selRes[w] = "ok" => selIdx[w] \in Chans /\ selItem[w] \in Range(received[selIdx[w]])
        /\ selRes[w] = "closed" => selIdx[w] \in Chans /\ closed[selIdx[w]]
        /\ selRes[w] \in {"closed", "timeout"} => selItem[w] = 0
    \* Each item handed out exactly once: by a selector or a plain receiver.
    SelectorsTakeDistinct == \A v, w \in Selectors :
        v # w /\ selItem[v] # 0 => selItem[v] # selItem[w]
    \* Only live waiters are registered; a returned select() is registered nowhere.
    WaiterLifetime ==
        /\ \A c \in Chans : regs[c] \subseteq {w \in Selectors : live[w]}
        /\ \A w \in Selectors : pc[w] = "Done" => ~live[w]
    \* Ownership: once every thread is done, every item is accounted for exactly
    \* once (delivered, released by the core, or still queued for chan_free).
    Conservation == (\A p \in Workers : pc[p] = "Done") =>
        \A s \in Items : Cardinality({k \in {"recv", "rel", "buf"} :
            \/ (k = "recv" /\ s \in AllReceived)
            \/ (k = "rel"  /\ s \in released)
            \/ (k = "buf"  /\ s \in AllBuffered)}) = 1
    RecvClosedOnlyWhenDrained == \A r \in Receivers :
        recvRes[r] = "closed" => closed[RecvChan(r)]
end define;

macro signal(v) begin
    if parked[v] # {} then
        with x \in parked[v] do parked[v] := parked[v] \ {x}; end with;
    end if;
end macro;

\* wake_senders(c): broadcast for rendezvous, signal otherwise.
macro wake_senders(c) begin
    if Caps[c] = 0 then parked[<<"nf", c>>] := {};
    else signal(<<"nf", c>>);
    end if;
end macro;

\* dequeue(c) into the receiver's history; recv_gen++; wake_senders.
macro take(c) begin
    received[c] := Append(received[c], Head(buf[c]));
    buf[c] := Tail(buf[c]);
    recvGen[c] := recvGen[c] + 1;
    wake_senders(c);
end macro;

\* wait_cond(): releases mutex mx, parks on cv, re-acquires mx. Sets timedOut.
procedure wait(cv, mx) begin
  W_park:
    parked[cv] := parked[cv] \cup {self};
    mtx[mx] := 0;
  W_wake:
    either
        await self \notin parked[cv];          \* signalled / broadcast
        timedOut[self] := FALSE;
    or
        await Timeouts;                        \* ETIMEDOUT (maybe after a signal)
        parked[cv] := parked[cv] \ {self};
        timedOut[self] := TRUE;
    or
        await Spurious /\ self \in parked[cv]; \* spurious wakeup
        parked[cv] := parked[cv] \ {self};
        timedOut[self] := FALSE;
    end either;
  W_relock:
    await mtx[mx] = 0;
    mtx[mx] := self;
    return;
end procedure;

\* notify_select_waiters(c), called with c's mutex held. For each registered
\* waiter: lock its mutex, set ready, signal its condvar, unlock.
procedure notify(nc)
variables toNotify = {};
begin
  N_snapshot:
    toNotify := regs[nc];
  N_loop:
    while toNotify # {} do
        with w \in toNotify do
            assert live[w];                   \* never touch a destroyed waiter
            await mtx[WM(w)] = 0;
            ready[w] := TRUE;
            signal(<<"w", w>>);
            toNotify := toNotify \ {w};
        end with;
    end while;
    return;
end procedure;

\* select_scan(): one atomic step per channel, under that channel's mutex.
procedure scan(start)
variables k = 0;
begin
  SC_init:
    k := 0; found[self] := FALSE;
  SC_loop:
    while k < NChans /\ ~found[self] do
        with c = ((start + k) % NChans) + 1 do
            await mtx[ChM(c)] = 0;
            if Count(c) > 0 then
                selItem[self] := Head(buf[c]);
                take(c);
                selIdx[self] := c; scanRes[self] := "ok"; found[self] := TRUE;
            elsif closed[c] then
                selIdx[self] := c; scanRes[self] := "closed"; found[self] := TRUE;
            end if;
        end with;
        k := k + 1;
    end while;
    return;
end procedure;

process sender \in Senders
variables staged = 0;
begin
  S_lock:
    await mtx[ChM(SendTo[self])] = 0; mtx[ChM(SendTo[self])] := self;
  S_phase1:
    while Count(SendTo[self]) = SlotCap(SendTo[self]) /\ ~closed[SendTo[self]] do
        call wait(<<"nf", SendTo[self]>>, ChM(SendTo[self]));
  S_phase1_check:
        if timedOut[self] /\ Count(SendTo[self]) = SlotCap(SendTo[self]) /\ ~closed[SendTo[self]] then
            mtx[ChM(SendTo[self])] := 0;
            released := released \cup {self};
            sendRes[self] := "timeout";
            goto S_end;
        end if;
    end while;
  S_closed_check:
    if closed[SendTo[self]] then
        mtx[ChM(SendTo[self])] := 0;
        released := released \cup {self};
        sendRes[self] := "closed";
        goto S_end;
    end if;
  S_enqueue:
    buf[SendTo[self]] := Append(buf[SendTo[self]], self);
    enqueued[SendTo[self]] := Append(enqueued[SendTo[self]], self);
    signal(<<"ne", SendTo[self]>>);
    call notify(SendTo[self]);                 \* wake_receivers()
  S_after_notify:
    if Caps[SendTo[self]] > 0 then
        mtx[ChM(SendTo[self])] := 0;
        sendRes[self] := "ok";
        goto S_end;
    end if;
  S_stage:
    staged := recvGen[SendTo[self]];
  S_phase2:
    while recvGen[SendTo[self]] = staged /\ ~closed[SendTo[self]] do
        call wait(<<"nf", SendTo[self]>>, ChM(SendTo[self]));
  S_phase2_check:
        if timedOut[self] /\ recvGen[SendTo[self]] = staged then
            \* withdraw_staged(): drop slot 0 without counting a receive
            assert buf[SendTo[self]] = <<self>>;   \* the slot still holds our item
            released := released \cup {Head(buf[SendTo[self]])};
            buf[SendTo[self]] := <<>>;
            wake_senders(SendTo[self]);
            mtx[ChM(SendTo[self])] := 0;
            sendRes[self] := "timeout";
            goto S_end;
        end if;
    end while;
  S_handoff_check:
    if recvGen[SendTo[self]] = staged then
        assert buf[SendTo[self]] = <<self>>;   \* the slot still holds our item
        released := released \cup {Head(buf[SendTo[self]])};
        buf[SendTo[self]] := <<>>;
        mtx[ChM(SendTo[self])] := 0;
        sendRes[self] := "closed";
        goto S_end;
    end if;
  S_ok:
    mtx[ChM(SendTo[self])] := 0;
    sendRes[self] := "ok";
  S_end:
    skip;
end process;

process receiver \in Receivers
begin
  R_lock:
    await mtx[ChM(RecvChan(self))] = 0; mtx[ChM(RecvChan(self))] := self;
  R_wait:
    while Count(RecvChan(self)) = 0 /\ ~closed[RecvChan(self)] do
        call wait(<<"ne", RecvChan(self)>>, ChM(RecvChan(self)));
  R_wait_check:
        if timedOut[self] /\ Count(RecvChan(self)) = 0 /\ ~closed[RecvChan(self)] then
            mtx[ChM(RecvChan(self))] := 0;
            recvRes[self] := "timeout";
            goto R_end;
        end if;
    end while;
  R_take:
    if Count(RecvChan(self)) = 0 then
        mtx[ChM(RecvChan(self))] := 0;
        recvRes[self] := "closed";
        goto R_end;
    end if;
  R_dequeue:
    take(RecvChan(self));
    mtx[ChM(RecvChan(self))] := 0;
    recvRes[self] := "ok";
  R_end:
    skip;
end process;

process selector \in Selectors
variables first = 0, woke = FALSE, reg = 1;
begin
  SEL_start:
    with s \in 0..(NChans - 1) do first := s; end with;
  SEL_fast_scan:
    call scan(first);                          \* fast path
  SEL_fast:
    if found[self] then
        selRes[self] := scanRes[self];
        goto SEL_end;
    end if;
  SEL_init:
    live[self] := TRUE; ready[self] := FALSE;
    reg := 1;
  SEL_register:
    while reg <= NChans do
        await mtx[ChM(reg)] = 0;
        regs[reg] := regs[reg] \cup {self};
        reg := reg + 1;
    end while;
  SEL_loop:
    while TRUE do
        call scan(first);
  SEL_scanned:
        if found[self] then
            selRes[self] := scanRes[self];
            goto SEL_unregister;
        end if;
  SEL_wlock:
        await mtx[WM(self)] = 0; mtx[WM(self)] := self;
  SEL_wait:
        while ~ready[self] do
            call wait(<<"w", self>>, WM(self));
  SEL_wait_check:
            if timedOut[self] then goto SEL_woke; end if;
        end while;
  SEL_woke:
        woke := ready[self];
        ready[self] := FALSE;
        mtx[WM(self)] := 0;
        if ~woke then
            selRes[self] := "timeout";
            goto SEL_unregister;
        end if;
    end while;
  SEL_unregister:
    reg := 1;
  SEL_unreg_loop:
    while reg <= NChans do
        await mtx[ChM(reg)] = 0;
        regs[reg] := regs[reg] \ {self};
        reg := reg + 1;
    end while;
  SEL_destroy:
    live[self] := FALSE;
  SEL_end:
    skip;
end process;

process closer = Closer
variables toClose = CloseSet, curClose = 0;
begin
  C_loop:
    while toClose # {} do
        \* hml_chan_close(): lock, set closed, broadcast both condvars,
        \* notify select waiters, unlock.
        with c = CHOOSE c \in toClose : \A d \in toClose : c <= d do
            await mtx[ChM(c)] = 0;
            mtx[ChM(c)] := Closer;
            closed[c] := TRUE;
            parked[<<"ne", c>>] := {} || parked[<<"nf", c>>] := {};
            toClose := toClose \ {c};
            curClose := c;
        end with;
  C_notify:
        call notify(curClose);
  C_unlock:
        mtx[ChM(curClose)] := 0;
    end while;
end process;

end algorithm; *)
\* BEGIN TRANSLATION
CONSTANT defaultInitValue
VARIABLES pc, mtx, parked, buf, closed, recvGen, regs, ready, live, enqueued, 
          received, released, sendRes, recvRes, selRes, selIdx, selItem, 
          timedOut, found, scanRes, stack

(* define statement *)
Count(c) == Len(buf[c])
Range(f) == {f[i] : i \in DOMAIN f}
AllReceived == UNION {Range(received[c]) : c \in Chans}
AllBuffered == UNION {Range(buf[c]) : c \in Chans}
Workers == Senders \cup Receivers \cup Selectors


TypeOK == \A c \in Chans : Count(c) <= SlotCap(c)

NoDuplicateDelivery ==
    /\ \A c \in Chans : \A i, j \in DOMAIN received[c] :
           i # j => received[c][i] # received[c][j]
    /\ \A c, d \in Chans : c # d => Range(received[c]) \cap Range(received[d]) = {}

DeliveredXorReleased == AllReceived \cap released = {}

RightChannel == \A c \in Chans : \A x \in Range(received[c]) : SendTo[x] = c


FIFO == \A c \in Chans :
    LET kept == SelectSeq(enqueued[c], LAMBDA x : x \notin released)
    IN received[c] = SubSeq(kept, 1, Len(received[c]))

SendContract == \A s \in Senders :
    /\ sendRes[s] = "ok" =>
         IF Caps[SendTo[s]] = 0 THEN s \in AllReceived
         ELSE s \in AllReceived \cup AllBuffered
    /\ sendRes[s] \in {"timeout", "closed"} =>
         s \in released /\ s \notin AllReceived


SelectContract == \A w \in Selectors :
    /\ selRes[w] = "ok" => selIdx[w] \in Chans /\ selItem[w] \in Range(received[selIdx[w]])
    /\ selRes[w] = "closed" => selIdx[w] \in Chans /\ closed[selIdx[w]]
    /\ selRes[w] \in {"closed", "timeout"} => selItem[w] = 0

SelectorsTakeDistinct == \A v, w \in Selectors :
    v # w /\ selItem[v] # 0 => selItem[v] # selItem[w]

WaiterLifetime ==
    /\ \A c \in Chans : regs[c] \subseteq {w \in Selectors : live[w]}
    /\ \A w \in Selectors : pc[w] = "Done" => ~live[w]


Conservation == (\A p \in Workers : pc[p] = "Done") =>
    \A s \in Items : Cardinality({k \in {"recv", "rel", "buf"} :
        \/ (k = "recv" /\ s \in AllReceived)
        \/ (k = "rel"  /\ s \in released)
        \/ (k = "buf"  /\ s \in AllBuffered)}) = 1
RecvClosedOnlyWhenDrained == \A r \in Receivers :
    recvRes[r] = "closed" => closed[RecvChan(r)]

VARIABLES cv, mx, nc, toNotify, start, k, staged, first, woke, reg, toClose, 
          curClose

vars == << pc, mtx, parked, buf, closed, recvGen, regs, ready, live, enqueued, 
           received, released, sendRes, recvRes, selRes, selIdx, selItem, 
           timedOut, found, scanRes, stack, cv, mx, nc, toNotify, start, k, 
           staged, first, woke, reg, toClose, curClose >>

ProcSet == (Senders) \cup (Receivers) \cup (Selectors) \cup {Closer}

Init == (* Global variables *)
        /\ mtx = [m \in Mutexes |-> 0]
        /\ parked = [v \in CVs |-> {}]
        /\ buf = [c \in Chans |-> <<>>]
        /\ closed = [c \in Chans |-> FALSE]
        /\ recvGen = [c \in Chans |-> 0]
        /\ regs = [c \in Chans |-> {}]
        /\ ready = [w \in Selectors |-> FALSE]
        /\ live = [w \in Selectors |-> FALSE]
        /\ enqueued = [c \in Chans |-> <<>>]
        /\ received = [c \in Chans |-> <<>>]
        /\ released = {}
        /\ sendRes = [s \in Senders |-> "none"]
        /\ recvRes = [r \in Receivers |-> "none"]
        /\ selRes = [w \in Selectors |-> "none"]
        /\ selIdx = [w \in Selectors |-> 0]
        /\ selItem = [w \in Selectors |-> 0]
        /\ timedOut = [p \in Senders \cup Receivers \cup Selectors |-> FALSE]
        /\ found = [w \in Selectors |-> FALSE]
        /\ scanRes = [w \in Selectors |-> "none"]
        (* Procedure wait *)
        /\ cv = [ self \in ProcSet |-> defaultInitValue]
        /\ mx = [ self \in ProcSet |-> defaultInitValue]
        (* Procedure notify *)
        /\ nc = [ self \in ProcSet |-> defaultInitValue]
        /\ toNotify = [ self \in ProcSet |-> {}]
        (* Procedure scan *)
        /\ start = [ self \in ProcSet |-> defaultInitValue]
        /\ k = [ self \in ProcSet |-> 0]
        (* Process sender *)
        /\ staged = [self \in Senders |-> 0]
        (* Process selector *)
        /\ first = [self \in Selectors |-> 0]
        /\ woke = [self \in Selectors |-> FALSE]
        /\ reg = [self \in Selectors |-> 1]
        (* Process closer *)
        /\ toClose = CloseSet
        /\ curClose = 0
        /\ stack = [self \in ProcSet |-> << >>]
        /\ pc = [self \in ProcSet |-> CASE self \in Senders -> "S_lock"
                                        [] self \in Receivers -> "R_lock"
                                        [] self \in Selectors -> "SEL_start"
                                        [] self = Closer -> "C_loop"]

W_park(self) == /\ pc[self] = "W_park"
                /\ parked' = [parked EXCEPT ![cv[self]] = parked[cv[self]] \cup {self}]
                /\ mtx' = [mtx EXCEPT ![mx[self]] = 0]
                /\ pc' = [pc EXCEPT ![self] = "W_wake"]
                /\ UNCHANGED << buf, closed, recvGen, regs, ready, live, 
                                enqueued, received, released, sendRes, recvRes, 
                                selRes, selIdx, selItem, timedOut, found, 
                                scanRes, stack, cv, mx, nc, toNotify, start, k, 
                                staged, first, woke, reg, toClose, curClose >>

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
                /\ UNCHANGED << mtx, buf, closed, recvGen, regs, ready, live, 
                                enqueued, received, released, sendRes, recvRes, 
                                selRes, selIdx, selItem, found, scanRes, stack, 
                                cv, mx, nc, toNotify, start, k, staged, first, 
                                woke, reg, toClose, curClose >>

W_relock(self) == /\ pc[self] = "W_relock"
                  /\ mtx[mx[self]] = 0
                  /\ mtx' = [mtx EXCEPT ![mx[self]] = self]
                  /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
                  /\ cv' = [cv EXCEPT ![self] = Head(stack[self]).cv]
                  /\ mx' = [mx EXCEPT ![self] = Head(stack[self]).mx]
                  /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
                  /\ UNCHANGED << parked, buf, closed, recvGen, regs, ready, 
                                  live, enqueued, received, released, sendRes, 
                                  recvRes, selRes, selIdx, selItem, timedOut, 
                                  found, scanRes, nc, toNotify, start, k, 
                                  staged, first, woke, reg, toClose, curClose >>

wait(self) == W_park(self) \/ W_wake(self) \/ W_relock(self)

N_snapshot(self) == /\ pc[self] = "N_snapshot"
                    /\ toNotify' = [toNotify EXCEPT ![self] = regs[nc[self]]]
                    /\ pc' = [pc EXCEPT ![self] = "N_loop"]
                    /\ UNCHANGED << mtx, parked, buf, closed, recvGen, regs, 
                                    ready, live, enqueued, received, released, 
                                    sendRes, recvRes, selRes, selIdx, selItem, 
                                    timedOut, found, scanRes, stack, cv, mx, 
                                    nc, start, k, staged, first, woke, reg, 
                                    toClose, curClose >>

N_loop(self) == /\ pc[self] = "N_loop"
                /\ IF toNotify[self] # {}
                      THEN /\ \E w \in toNotify[self]:
                                /\ Assert(live[w], 
                                          "Failure of assertion at line 193, column 13.")
                                /\ mtx[WM(w)] = 0
                                /\ ready' = [ready EXCEPT ![w] = TRUE]
                                /\ IF parked[(<<"w", w>>)] # {}
                                      THEN /\ \E x \in parked[(<<"w", w>>)]:
                                                parked' = [parked EXCEPT ![(<<"w", w>>)] = parked[(<<"w", w>>)] \ {x}]
                                      ELSE /\ TRUE
                                           /\ UNCHANGED parked
                                /\ toNotify' = [toNotify EXCEPT ![self] = toNotify[self] \ {w}]
                           /\ pc' = [pc EXCEPT ![self] = "N_loop"]
                           /\ UNCHANGED << stack, nc >>
                      ELSE /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
                           /\ toNotify' = [toNotify EXCEPT ![self] = Head(stack[self]).toNotify]
                           /\ nc' = [nc EXCEPT ![self] = Head(stack[self]).nc]
                           /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
                           /\ UNCHANGED << parked, ready >>
                /\ UNCHANGED << mtx, buf, closed, recvGen, regs, live, 
                                enqueued, received, released, sendRes, recvRes, 
                                selRes, selIdx, selItem, timedOut, found, 
                                scanRes, cv, mx, start, k, staged, first, woke, 
                                reg, toClose, curClose >>

notify(self) == N_snapshot(self) \/ N_loop(self)

SC_init(self) == /\ pc[self] = "SC_init"
                 /\ k' = [k EXCEPT ![self] = 0]
                 /\ found' = [found EXCEPT ![self] = FALSE]
                 /\ pc' = [pc EXCEPT ![self] = "SC_loop"]
                 /\ UNCHANGED << mtx, parked, buf, closed, recvGen, regs, 
                                 ready, live, enqueued, received, released, 
                                 sendRes, recvRes, selRes, selIdx, selItem, 
                                 timedOut, scanRes, stack, cv, mx, nc, 
                                 toNotify, start, staged, first, woke, reg, 
                                 toClose, curClose >>

SC_loop(self) == /\ pc[self] = "SC_loop"
                 /\ IF k[self] < NChans /\ ~found[self]
                       THEN /\ LET c == ((start[self] + k[self]) % NChans) + 1 IN
                                 /\ mtx[ChM(c)] = 0
                                 /\ IF Count(c) > 0
                                       THEN /\ selItem' = [selItem EXCEPT ![self] = Head(buf[c])]
                                            /\ received' = [received EXCEPT ![c] = Append(received[c], Head(buf[c]))]
                                            /\ buf' = [buf EXCEPT ![c] = Tail(buf[c])]
                                            /\ recvGen' = [recvGen EXCEPT ![c] = recvGen[c] + 1]
                                            /\ IF Caps[c] = 0
                                                  THEN /\ parked' = [parked EXCEPT ![<<"nf", c>>] = {}]
                                                  ELSE /\ IF parked[(<<"nf", c>>)] # {}
                                                             THEN /\ \E x \in parked[(<<"nf", c>>)]:
                                                                       parked' = [parked EXCEPT ![(<<"nf", c>>)] = parked[(<<"nf", c>>)] \ {x}]
                                                             ELSE /\ TRUE
                                                                  /\ UNCHANGED parked
                                            /\ selIdx' = [selIdx EXCEPT ![self] = c]
                                            /\ scanRes' = [scanRes EXCEPT ![self] = "ok"]
                                            /\ found' = [found EXCEPT ![self] = TRUE]
                                       ELSE /\ IF closed[c]
                                                  THEN /\ selIdx' = [selIdx EXCEPT ![self] = c]
                                                       /\ scanRes' = [scanRes EXCEPT ![self] = "closed"]
                                                       /\ found' = [found EXCEPT ![self] = TRUE]
                                                  ELSE /\ TRUE
                                                       /\ UNCHANGED << selIdx, 
                                                                       found, 
                                                                       scanRes >>
                                            /\ UNCHANGED << parked, buf, 
                                                            recvGen, received, 
                                                            selItem >>
                            /\ k' = [k EXCEPT ![self] = k[self] + 1]
                            /\ pc' = [pc EXCEPT ![self] = "SC_loop"]
                            /\ UNCHANGED << stack, start >>
                       ELSE /\ pc' = [pc EXCEPT ![self] = Head(stack[self]).pc]
                            /\ k' = [k EXCEPT ![self] = Head(stack[self]).k]
                            /\ start' = [start EXCEPT ![self] = Head(stack[self]).start]
                            /\ stack' = [stack EXCEPT ![self] = Tail(stack[self])]
                            /\ UNCHANGED << parked, buf, recvGen, received, 
                                            selIdx, selItem, found, scanRes >>
                 /\ UNCHANGED << mtx, closed, regs, ready, live, enqueued, 
                                 released, sendRes, recvRes, selRes, timedOut, 
                                 cv, mx, nc, toNotify, staged, first, woke, 
                                 reg, toClose, curClose >>

scan(self) == SC_init(self) \/ SC_loop(self)

S_lock(self) == /\ pc[self] = "S_lock"
                /\ mtx[ChM(SendTo[self])] = 0
                /\ mtx' = [mtx EXCEPT ![ChM(SendTo[self])] = self]
                /\ pc' = [pc EXCEPT ![self] = "S_phase1"]
                /\ UNCHANGED << parked, buf, closed, recvGen, regs, ready, 
                                live, enqueued, received, released, sendRes, 
                                recvRes, selRes, selIdx, selItem, timedOut, 
                                found, scanRes, stack, cv, mx, nc, toNotify, 
                                start, k, staged, first, woke, reg, toClose, 
                                curClose >>

S_phase1(self) == /\ pc[self] = "S_phase1"
                  /\ IF Count(SendTo[self]) = SlotCap(SendTo[self]) /\ ~closed[SendTo[self]]
                        THEN /\ /\ cv' = [cv EXCEPT ![self] = <<"nf", SendTo[self]>>]
                                /\ mx' = [mx EXCEPT ![self] = ChM(SendTo[self])]
                                /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "wait",
                                                                         pc        |->  "S_phase1_check",
                                                                         cv        |->  cv[self],
                                                                         mx        |->  mx[self] ] >>
                                                                     \o stack[self]]
                             /\ pc' = [pc EXCEPT ![self] = "W_park"]
                        ELSE /\ pc' = [pc EXCEPT ![self] = "S_closed_check"]
                             /\ UNCHANGED << stack, cv, mx >>
                  /\ UNCHANGED << mtx, parked, buf, closed, recvGen, regs, 
                                  ready, live, enqueued, received, released, 
                                  sendRes, recvRes, selRes, selIdx, selItem, 
                                  timedOut, found, scanRes, nc, toNotify, 
                                  start, k, staged, first, woke, reg, toClose, 
                                  curClose >>

S_phase1_check(self) == /\ pc[self] = "S_phase1_check"
                        /\ IF timedOut[self] /\ Count(SendTo[self]) = SlotCap(SendTo[self]) /\ ~closed[SendTo[self]]
                              THEN /\ mtx' = [mtx EXCEPT ![ChM(SendTo[self])] = 0]
                                   /\ released' = (released \cup {self})
                                   /\ sendRes' = [sendRes EXCEPT ![self] = "timeout"]
                                   /\ pc' = [pc EXCEPT ![self] = "S_end"]
                              ELSE /\ pc' = [pc EXCEPT ![self] = "S_phase1"]
                                   /\ UNCHANGED << mtx, released, sendRes >>
                        /\ UNCHANGED << parked, buf, closed, recvGen, regs, 
                                        ready, live, enqueued, received, 
                                        recvRes, selRes, selIdx, selItem, 
                                        timedOut, found, scanRes, stack, cv, 
                                        mx, nc, toNotify, start, k, staged, 
                                        first, woke, reg, toClose, curClose >>

S_closed_check(self) == /\ pc[self] = "S_closed_check"
                        /\ IF closed[SendTo[self]]
                              THEN /\ mtx' = [mtx EXCEPT ![ChM(SendTo[self])] = 0]
                                   /\ released' = (released \cup {self})
                                   /\ sendRes' = [sendRes EXCEPT ![self] = "closed"]
                                   /\ pc' = [pc EXCEPT ![self] = "S_end"]
                              ELSE /\ pc' = [pc EXCEPT ![self] = "S_enqueue"]
                                   /\ UNCHANGED << mtx, released, sendRes >>
                        /\ UNCHANGED << parked, buf, closed, recvGen, regs, 
                                        ready, live, enqueued, received, 
                                        recvRes, selRes, selIdx, selItem, 
                                        timedOut, found, scanRes, stack, cv, 
                                        mx, nc, toNotify, start, k, staged, 
                                        first, woke, reg, toClose, curClose >>

S_enqueue(self) == /\ pc[self] = "S_enqueue"
                   /\ buf' = [buf EXCEPT ![SendTo[self]] = Append(buf[SendTo[self]], self)]
                   /\ enqueued' = [enqueued EXCEPT ![SendTo[self]] = Append(enqueued[SendTo[self]], self)]
                   /\ IF parked[(<<"ne", SendTo[self]>>)] # {}
                         THEN /\ \E x \in parked[(<<"ne", SendTo[self]>>)]:
                                   parked' = [parked EXCEPT ![(<<"ne", SendTo[self]>>)] = parked[(<<"ne", SendTo[self]>>)] \ {x}]
                         ELSE /\ TRUE
                              /\ UNCHANGED parked
                   /\ /\ nc' = [nc EXCEPT ![self] = SendTo[self]]
                      /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "notify",
                                                               pc        |->  "S_after_notify",
                                                               toNotify  |->  toNotify[self],
                                                               nc        |->  nc[self] ] >>
                                                           \o stack[self]]
                   /\ toNotify' = [toNotify EXCEPT ![self] = {}]
                   /\ pc' = [pc EXCEPT ![self] = "N_snapshot"]
                   /\ UNCHANGED << mtx, closed, recvGen, regs, ready, live, 
                                   received, released, sendRes, recvRes, 
                                   selRes, selIdx, selItem, timedOut, found, 
                                   scanRes, cv, mx, start, k, staged, first, 
                                   woke, reg, toClose, curClose >>

S_after_notify(self) == /\ pc[self] = "S_after_notify"
                        /\ IF Caps[SendTo[self]] > 0
                              THEN /\ mtx' = [mtx EXCEPT ![ChM(SendTo[self])] = 0]
                                   /\ sendRes' = [sendRes EXCEPT ![self] = "ok"]
                                   /\ pc' = [pc EXCEPT ![self] = "S_end"]
                              ELSE /\ pc' = [pc EXCEPT ![self] = "S_stage"]
                                   /\ UNCHANGED << mtx, sendRes >>
                        /\ UNCHANGED << parked, buf, closed, recvGen, regs, 
                                        ready, live, enqueued, received, 
                                        released, recvRes, selRes, selIdx, 
                                        selItem, timedOut, found, scanRes, 
                                        stack, cv, mx, nc, toNotify, start, k, 
                                        staged, first, woke, reg, toClose, 
                                        curClose >>

S_stage(self) == /\ pc[self] = "S_stage"
                 /\ staged' = [staged EXCEPT ![self] = recvGen[SendTo[self]]]
                 /\ pc' = [pc EXCEPT ![self] = "S_phase2"]
                 /\ UNCHANGED << mtx, parked, buf, closed, recvGen, regs, 
                                 ready, live, enqueued, received, released, 
                                 sendRes, recvRes, selRes, selIdx, selItem, 
                                 timedOut, found, scanRes, stack, cv, mx, nc, 
                                 toNotify, start, k, first, woke, reg, toClose, 
                                 curClose >>

S_phase2(self) == /\ pc[self] = "S_phase2"
                  /\ IF recvGen[SendTo[self]] = staged[self] /\ ~closed[SendTo[self]]
                        THEN /\ /\ cv' = [cv EXCEPT ![self] = <<"nf", SendTo[self]>>]
                                /\ mx' = [mx EXCEPT ![self] = ChM(SendTo[self])]
                                /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "wait",
                                                                         pc        |->  "S_phase2_check",
                                                                         cv        |->  cv[self],
                                                                         mx        |->  mx[self] ] >>
                                                                     \o stack[self]]
                             /\ pc' = [pc EXCEPT ![self] = "W_park"]
                        ELSE /\ pc' = [pc EXCEPT ![self] = "S_handoff_check"]
                             /\ UNCHANGED << stack, cv, mx >>
                  /\ UNCHANGED << mtx, parked, buf, closed, recvGen, regs, 
                                  ready, live, enqueued, received, released, 
                                  sendRes, recvRes, selRes, selIdx, selItem, 
                                  timedOut, found, scanRes, nc, toNotify, 
                                  start, k, staged, first, woke, reg, toClose, 
                                  curClose >>

S_phase2_check(self) == /\ pc[self] = "S_phase2_check"
                        /\ IF timedOut[self] /\ recvGen[SendTo[self]] = staged[self]
                              THEN /\ Assert(buf[SendTo[self]] = <<self>>, 
                                             "Failure of assertion at line 268, column 13.")
                                   /\ released' = (released \cup {Head(buf[SendTo[self]])})
                                   /\ buf' = [buf EXCEPT ![SendTo[self]] = <<>>]
                                   /\ IF Caps[(SendTo[self])] = 0
                                         THEN /\ parked' = [parked EXCEPT ![<<"nf", (SendTo[self])>>] = {}]
                                         ELSE /\ IF parked[(<<"nf", (SendTo[self])>>)] # {}
                                                    THEN /\ \E x \in parked[(<<"nf", (SendTo[self])>>)]:
                                                              parked' = [parked EXCEPT ![(<<"nf", (SendTo[self])>>)] = parked[(<<"nf", (SendTo[self])>>)] \ {x}]
                                                    ELSE /\ TRUE
                                                         /\ UNCHANGED parked
                                   /\ mtx' = [mtx EXCEPT ![ChM(SendTo[self])] = 0]
                                   /\ sendRes' = [sendRes EXCEPT ![self] = "timeout"]
                                   /\ pc' = [pc EXCEPT ![self] = "S_end"]
                              ELSE /\ pc' = [pc EXCEPT ![self] = "S_phase2"]
                                   /\ UNCHANGED << mtx, parked, buf, released, 
                                                   sendRes >>
                        /\ UNCHANGED << closed, recvGen, regs, ready, live, 
                                        enqueued, received, recvRes, selRes, 
                                        selIdx, selItem, timedOut, found, 
                                        scanRes, stack, cv, mx, nc, toNotify, 
                                        start, k, staged, first, woke, reg, 
                                        toClose, curClose >>

S_handoff_check(self) == /\ pc[self] = "S_handoff_check"
                         /\ IF recvGen[SendTo[self]] = staged[self]
                               THEN /\ Assert(buf[SendTo[self]] = <<self>>, 
                                              "Failure of assertion at line 279, column 9.")
                                    /\ released' = (released \cup {Head(buf[SendTo[self]])})
                                    /\ buf' = [buf EXCEPT ![SendTo[self]] = <<>>]
                                    /\ mtx' = [mtx EXCEPT ![ChM(SendTo[self])] = 0]
                                    /\ sendRes' = [sendRes EXCEPT ![self] = "closed"]
                                    /\ pc' = [pc EXCEPT ![self] = "S_end"]
                               ELSE /\ pc' = [pc EXCEPT ![self] = "S_ok"]
                                    /\ UNCHANGED << mtx, buf, released, 
                                                    sendRes >>
                         /\ UNCHANGED << parked, closed, recvGen, regs, ready, 
                                         live, enqueued, received, recvRes, 
                                         selRes, selIdx, selItem, timedOut, 
                                         found, scanRes, stack, cv, mx, nc, 
                                         toNotify, start, k, staged, first, 
                                         woke, reg, toClose, curClose >>

S_ok(self) == /\ pc[self] = "S_ok"
              /\ mtx' = [mtx EXCEPT ![ChM(SendTo[self])] = 0]
              /\ sendRes' = [sendRes EXCEPT ![self] = "ok"]
              /\ pc' = [pc EXCEPT ![self] = "S_end"]
              /\ UNCHANGED << parked, buf, closed, recvGen, regs, ready, live, 
                              enqueued, received, released, recvRes, selRes, 
                              selIdx, selItem, timedOut, found, scanRes, stack, 
                              cv, mx, nc, toNotify, start, k, staged, first, 
                              woke, reg, toClose, curClose >>

S_end(self) == /\ pc[self] = "S_end"
               /\ TRUE
               /\ pc' = [pc EXCEPT ![self] = "Done"]
               /\ UNCHANGED << mtx, parked, buf, closed, recvGen, regs, ready, 
                               live, enqueued, received, released, sendRes, 
                               recvRes, selRes, selIdx, selItem, timedOut, 
                               found, scanRes, stack, cv, mx, nc, toNotify, 
                               start, k, staged, first, woke, reg, toClose, 
                               curClose >>

sender(self) == S_lock(self) \/ S_phase1(self) \/ S_phase1_check(self)
                   \/ S_closed_check(self) \/ S_enqueue(self)
                   \/ S_after_notify(self) \/ S_stage(self)
                   \/ S_phase2(self) \/ S_phase2_check(self)
                   \/ S_handoff_check(self) \/ S_ok(self) \/ S_end(self)

R_lock(self) == /\ pc[self] = "R_lock"
                /\ mtx[ChM(RecvChan(self))] = 0
                /\ mtx' = [mtx EXCEPT ![ChM(RecvChan(self))] = self]
                /\ pc' = [pc EXCEPT ![self] = "R_wait"]
                /\ UNCHANGED << parked, buf, closed, recvGen, regs, ready, 
                                live, enqueued, received, released, sendRes, 
                                recvRes, selRes, selIdx, selItem, timedOut, 
                                found, scanRes, stack, cv, mx, nc, toNotify, 
                                start, k, staged, first, woke, reg, toClose, 
                                curClose >>

R_wait(self) == /\ pc[self] = "R_wait"
                /\ IF Count(RecvChan(self)) = 0 /\ ~closed[RecvChan(self)]
                      THEN /\ /\ cv' = [cv EXCEPT ![self] = <<"ne", RecvChan(self)>>]
                              /\ mx' = [mx EXCEPT ![self] = ChM(RecvChan(self))]
                              /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "wait",
                                                                       pc        |->  "R_wait_check",
                                                                       cv        |->  cv[self],
                                                                       mx        |->  mx[self] ] >>
                                                                   \o stack[self]]
                           /\ pc' = [pc EXCEPT ![self] = "W_park"]
                      ELSE /\ pc' = [pc EXCEPT ![self] = "R_take"]
                           /\ UNCHANGED << stack, cv, mx >>
                /\ UNCHANGED << mtx, parked, buf, closed, recvGen, regs, ready, 
                                live, enqueued, received, released, sendRes, 
                                recvRes, selRes, selIdx, selItem, timedOut, 
                                found, scanRes, nc, toNotify, start, k, staged, 
                                first, woke, reg, toClose, curClose >>

R_wait_check(self) == /\ pc[self] = "R_wait_check"
                      /\ IF timedOut[self] /\ Count(RecvChan(self)) = 0 /\ ~closed[RecvChan(self)]
                            THEN /\ mtx' = [mtx EXCEPT ![ChM(RecvChan(self))] = 0]
                                 /\ recvRes' = [recvRes EXCEPT ![self] = "timeout"]
                                 /\ pc' = [pc EXCEPT ![self] = "R_end"]
                            ELSE /\ pc' = [pc EXCEPT ![self] = "R_wait"]
                                 /\ UNCHANGED << mtx, recvRes >>
                      /\ UNCHANGED << parked, buf, closed, recvGen, regs, 
                                      ready, live, enqueued, received, 
                                      released, sendRes, selRes, selIdx, 
                                      selItem, timedOut, found, scanRes, stack, 
                                      cv, mx, nc, toNotify, start, k, staged, 
                                      first, woke, reg, toClose, curClose >>

R_take(self) == /\ pc[self] = "R_take"
                /\ IF Count(RecvChan(self)) = 0
                      THEN /\ mtx' = [mtx EXCEPT ![ChM(RecvChan(self))] = 0]
                           /\ recvRes' = [recvRes EXCEPT ![self] = "closed"]
                           /\ pc' = [pc EXCEPT ![self] = "R_end"]
                      ELSE /\ pc' = [pc EXCEPT ![self] = "R_dequeue"]
                           /\ UNCHANGED << mtx, recvRes >>
                /\ UNCHANGED << parked, buf, closed, recvGen, regs, ready, 
                                live, enqueued, received, released, sendRes, 
                                selRes, selIdx, selItem, timedOut, found, 
                                scanRes, stack, cv, mx, nc, toNotify, start, k, 
                                staged, first, woke, reg, toClose, curClose >>

R_dequeue(self) == /\ pc[self] = "R_dequeue"
                   /\ received' = [received EXCEPT ![(RecvChan(self))] = Append(received[(RecvChan(self))], Head(buf[(RecvChan(self))]))]
                   /\ buf' = [buf EXCEPT ![(RecvChan(self))] = Tail(buf[(RecvChan(self))])]
                   /\ recvGen' = [recvGen EXCEPT ![(RecvChan(self))] = recvGen[(RecvChan(self))] + 1]
                   /\ IF Caps[(RecvChan(self))] = 0
                         THEN /\ parked' = [parked EXCEPT ![<<"nf", (RecvChan(self))>>] = {}]
                         ELSE /\ IF parked[(<<"nf", (RecvChan(self))>>)] # {}
                                    THEN /\ \E x \in parked[(<<"nf", (RecvChan(self))>>)]:
                                              parked' = [parked EXCEPT ![(<<"nf", (RecvChan(self))>>)] = parked[(<<"nf", (RecvChan(self))>>)] \ {x}]
                                    ELSE /\ TRUE
                                         /\ UNCHANGED parked
                   /\ mtx' = [mtx EXCEPT ![ChM(RecvChan(self))] = 0]
                   /\ recvRes' = [recvRes EXCEPT ![self] = "ok"]
                   /\ pc' = [pc EXCEPT ![self] = "R_end"]
                   /\ UNCHANGED << closed, regs, ready, live, enqueued, 
                                   released, sendRes, selRes, selIdx, selItem, 
                                   timedOut, found, scanRes, stack, cv, mx, nc, 
                                   toNotify, start, k, staged, first, woke, 
                                   reg, toClose, curClose >>

R_end(self) == /\ pc[self] = "R_end"
               /\ TRUE
               /\ pc' = [pc EXCEPT ![self] = "Done"]
               /\ UNCHANGED << mtx, parked, buf, closed, recvGen, regs, ready, 
                               live, enqueued, received, released, sendRes, 
                               recvRes, selRes, selIdx, selItem, timedOut, 
                               found, scanRes, stack, cv, mx, nc, toNotify, 
                               start, k, staged, first, woke, reg, toClose, 
                               curClose >>

receiver(self) == R_lock(self) \/ R_wait(self) \/ R_wait_check(self)
                     \/ R_take(self) \/ R_dequeue(self) \/ R_end(self)

SEL_start(self) == /\ pc[self] = "SEL_start"
                   /\ \E s \in 0..(NChans - 1):
                        first' = [first EXCEPT ![self] = s]
                   /\ pc' = [pc EXCEPT ![self] = "SEL_fast_scan"]
                   /\ UNCHANGED << mtx, parked, buf, closed, recvGen, regs, 
                                   ready, live, enqueued, received, released, 
                                   sendRes, recvRes, selRes, selIdx, selItem, 
                                   timedOut, found, scanRes, stack, cv, mx, nc, 
                                   toNotify, start, k, staged, woke, reg, 
                                   toClose, curClose >>

SEL_fast_scan(self) == /\ pc[self] = "SEL_fast_scan"
                       /\ /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "scan",
                                                                   pc        |->  "SEL_fast",
                                                                   k         |->  k[self],
                                                                   start     |->  start[self] ] >>
                                                               \o stack[self]]
                          /\ start' = [start EXCEPT ![self] = first[self]]
                       /\ k' = [k EXCEPT ![self] = 0]
                       /\ pc' = [pc EXCEPT ![self] = "SC_init"]
                       /\ UNCHANGED << mtx, parked, buf, closed, recvGen, regs, 
                                       ready, live, enqueued, received, 
                                       released, sendRes, recvRes, selRes, 
                                       selIdx, selItem, timedOut, found, 
                                       scanRes, cv, mx, nc, toNotify, staged, 
                                       first, woke, reg, toClose, curClose >>

SEL_fast(self) == /\ pc[self] = "SEL_fast"
                  /\ IF found[self]
                        THEN /\ selRes' = [selRes EXCEPT ![self] = scanRes[self]]
                             /\ pc' = [pc EXCEPT ![self] = "SEL_end"]
                        ELSE /\ pc' = [pc EXCEPT ![self] = "SEL_init"]
                             /\ UNCHANGED selRes
                  /\ UNCHANGED << mtx, parked, buf, closed, recvGen, regs, 
                                  ready, live, enqueued, received, released, 
                                  sendRes, recvRes, selIdx, selItem, timedOut, 
                                  found, scanRes, stack, cv, mx, nc, toNotify, 
                                  start, k, staged, first, woke, reg, toClose, 
                                  curClose >>

SEL_init(self) == /\ pc[self] = "SEL_init"
                  /\ live' = [live EXCEPT ![self] = TRUE]
                  /\ ready' = [ready EXCEPT ![self] = FALSE]
                  /\ reg' = [reg EXCEPT ![self] = 1]
                  /\ pc' = [pc EXCEPT ![self] = "SEL_register"]
                  /\ UNCHANGED << mtx, parked, buf, closed, recvGen, regs, 
                                  enqueued, received, released, sendRes, 
                                  recvRes, selRes, selIdx, selItem, timedOut, 
                                  found, scanRes, stack, cv, mx, nc, toNotify, 
                                  start, k, staged, first, woke, toClose, 
                                  curClose >>

SEL_register(self) == /\ pc[self] = "SEL_register"
                      /\ IF reg[self] <= NChans
                            THEN /\ mtx[ChM(reg[self])] = 0
                                 /\ regs' = [regs EXCEPT ![reg[self]] = regs[reg[self]] \cup {self}]
                                 /\ reg' = [reg EXCEPT ![self] = reg[self] + 1]
                                 /\ pc' = [pc EXCEPT ![self] = "SEL_register"]
                            ELSE /\ pc' = [pc EXCEPT ![self] = "SEL_loop"]
                                 /\ UNCHANGED << regs, reg >>
                      /\ UNCHANGED << mtx, parked, buf, closed, recvGen, ready, 
                                      live, enqueued, received, released, 
                                      sendRes, recvRes, selRes, selIdx, 
                                      selItem, timedOut, found, scanRes, stack, 
                                      cv, mx, nc, toNotify, start, k, staged, 
                                      first, woke, toClose, curClose >>

SEL_loop(self) == /\ pc[self] = "SEL_loop"
                  /\ /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "scan",
                                                              pc        |->  "SEL_scanned",
                                                              k         |->  k[self],
                                                              start     |->  start[self] ] >>
                                                          \o stack[self]]
                     /\ start' = [start EXCEPT ![self] = first[self]]
                  /\ k' = [k EXCEPT ![self] = 0]
                  /\ pc' = [pc EXCEPT ![self] = "SC_init"]
                  /\ UNCHANGED << mtx, parked, buf, closed, recvGen, regs, 
                                  ready, live, enqueued, received, released, 
                                  sendRes, recvRes, selRes, selIdx, selItem, 
                                  timedOut, found, scanRes, cv, mx, nc, 
                                  toNotify, staged, first, woke, reg, toClose, 
                                  curClose >>

SEL_scanned(self) == /\ pc[self] = "SEL_scanned"
                     /\ IF found[self]
                           THEN /\ selRes' = [selRes EXCEPT ![self] = scanRes[self]]
                                /\ pc' = [pc EXCEPT ![self] = "SEL_unregister"]
                           ELSE /\ pc' = [pc EXCEPT ![self] = "SEL_wlock"]
                                /\ UNCHANGED selRes
                     /\ UNCHANGED << mtx, parked, buf, closed, recvGen, regs, 
                                     ready, live, enqueued, received, released, 
                                     sendRes, recvRes, selIdx, selItem, 
                                     timedOut, found, scanRes, stack, cv, mx, 
                                     nc, toNotify, start, k, staged, first, 
                                     woke, reg, toClose, curClose >>

SEL_wlock(self) == /\ pc[self] = "SEL_wlock"
                   /\ mtx[WM(self)] = 0
                   /\ mtx' = [mtx EXCEPT ![WM(self)] = self]
                   /\ pc' = [pc EXCEPT ![self] = "SEL_wait"]
                   /\ UNCHANGED << parked, buf, closed, recvGen, regs, ready, 
                                   live, enqueued, received, released, sendRes, 
                                   recvRes, selRes, selIdx, selItem, timedOut, 
                                   found, scanRes, stack, cv, mx, nc, toNotify, 
                                   start, k, staged, first, woke, reg, toClose, 
                                   curClose >>

SEL_wait(self) == /\ pc[self] = "SEL_wait"
                  /\ IF ~ready[self]
                        THEN /\ /\ cv' = [cv EXCEPT ![self] = <<"w", self>>]
                                /\ mx' = [mx EXCEPT ![self] = WM(self)]
                                /\ stack' = [stack EXCEPT ![self] = << [ procedure |->  "wait",
                                                                         pc        |->  "SEL_wait_check",
                                                                         cv        |->  cv[self],
                                                                         mx        |->  mx[self] ] >>
                                                                     \o stack[self]]
                             /\ pc' = [pc EXCEPT ![self] = "W_park"]
                        ELSE /\ pc' = [pc EXCEPT ![self] = "SEL_woke"]
                             /\ UNCHANGED << stack, cv, mx >>
                  /\ UNCHANGED << mtx, parked, buf, closed, recvGen, regs, 
                                  ready, live, enqueued, received, released, 
                                  sendRes, recvRes, selRes, selIdx, selItem, 
                                  timedOut, found, scanRes, nc, toNotify, 
                                  start, k, staged, first, woke, reg, toClose, 
                                  curClose >>

SEL_wait_check(self) == /\ pc[self] = "SEL_wait_check"
                        /\ IF timedOut[self]
                              THEN /\ pc' = [pc EXCEPT ![self] = "SEL_woke"]
                              ELSE /\ pc' = [pc EXCEPT ![self] = "SEL_wait"]
                        /\ UNCHANGED << mtx, parked, buf, closed, recvGen, 
                                        regs, ready, live, enqueued, received, 
                                        released, sendRes, recvRes, selRes, 
                                        selIdx, selItem, timedOut, found, 
                                        scanRes, stack, cv, mx, nc, toNotify, 
                                        start, k, staged, first, woke, reg, 
                                        toClose, curClose >>

SEL_woke(self) == /\ pc[self] = "SEL_woke"
                  /\ woke' = [woke EXCEPT ![self] = ready[self]]
                  /\ ready' = [ready EXCEPT ![self] = FALSE]
                  /\ mtx' = [mtx EXCEPT ![WM(self)] = 0]
                  /\ IF ~woke'[self]
                        THEN /\ selRes' = [selRes EXCEPT ![self] = "timeout"]
                             /\ pc' = [pc EXCEPT ![self] = "SEL_unregister"]
                        ELSE /\ pc' = [pc EXCEPT ![self] = "SEL_loop"]
                             /\ UNCHANGED selRes
                  /\ UNCHANGED << parked, buf, closed, recvGen, regs, live, 
                                  enqueued, received, released, sendRes, 
                                  recvRes, selIdx, selItem, timedOut, found, 
                                  scanRes, stack, cv, mx, nc, toNotify, start, 
                                  k, staged, first, reg, toClose, curClose >>

SEL_unregister(self) == /\ pc[self] = "SEL_unregister"
                        /\ reg' = [reg EXCEPT ![self] = 1]
                        /\ pc' = [pc EXCEPT ![self] = "SEL_unreg_loop"]
                        /\ UNCHANGED << mtx, parked, buf, closed, recvGen, 
                                        regs, ready, live, enqueued, received, 
                                        released, sendRes, recvRes, selRes, 
                                        selIdx, selItem, timedOut, found, 
                                        scanRes, stack, cv, mx, nc, toNotify, 
                                        start, k, staged, first, woke, toClose, 
                                        curClose >>

SEL_unreg_loop(self) == /\ pc[self] = "SEL_unreg_loop"
                        /\ IF reg[self] <= NChans
                              THEN /\ mtx[ChM(reg[self])] = 0
                                   /\ regs' = [regs EXCEPT ![reg[self]] = regs[reg[self]] \ {self}]
                                   /\ reg' = [reg EXCEPT ![self] = reg[self] + 1]
                                   /\ pc' = [pc EXCEPT ![self] = "SEL_unreg_loop"]
                              ELSE /\ pc' = [pc EXCEPT ![self] = "SEL_destroy"]
                                   /\ UNCHANGED << regs, reg >>
                        /\ UNCHANGED << mtx, parked, buf, closed, recvGen, 
                                        ready, live, enqueued, received, 
                                        released, sendRes, recvRes, selRes, 
                                        selIdx, selItem, timedOut, found, 
                                        scanRes, stack, cv, mx, nc, toNotify, 
                                        start, k, staged, first, woke, toClose, 
                                        curClose >>

SEL_destroy(self) == /\ pc[self] = "SEL_destroy"
                     /\ live' = [live EXCEPT ![self] = FALSE]
                     /\ pc' = [pc EXCEPT ![self] = "SEL_end"]
                     /\ UNCHANGED << mtx, parked, buf, closed, recvGen, regs, 
                                     ready, enqueued, received, released, 
                                     sendRes, recvRes, selRes, selIdx, selItem, 
                                     timedOut, found, scanRes, stack, cv, mx, 
                                     nc, toNotify, start, k, staged, first, 
                                     woke, reg, toClose, curClose >>

SEL_end(self) == /\ pc[self] = "SEL_end"
                 /\ TRUE
                 /\ pc' = [pc EXCEPT ![self] = "Done"]
                 /\ UNCHANGED << mtx, parked, buf, closed, recvGen, regs, 
                                 ready, live, enqueued, received, released, 
                                 sendRes, recvRes, selRes, selIdx, selItem, 
                                 timedOut, found, scanRes, stack, cv, mx, nc, 
                                 toNotify, start, k, staged, first, woke, reg, 
                                 toClose, curClose >>

selector(self) == SEL_start(self) \/ SEL_fast_scan(self) \/ SEL_fast(self)
                     \/ SEL_init(self) \/ SEL_register(self)
                     \/ SEL_loop(self) \/ SEL_scanned(self)
                     \/ SEL_wlock(self) \/ SEL_wait(self)
                     \/ SEL_wait_check(self) \/ SEL_woke(self)
                     \/ SEL_unregister(self) \/ SEL_unreg_loop(self)
                     \/ SEL_destroy(self) \/ SEL_end(self)

C_loop == /\ pc[Closer] = "C_loop"
          /\ IF toClose # {}
                THEN /\ LET c == CHOOSE c \in toClose : \A d \in toClose : c <= d IN
                          /\ mtx[ChM(c)] = 0
                          /\ mtx' = [mtx EXCEPT ![ChM(c)] = Closer]
                          /\ closed' = [closed EXCEPT ![c] = TRUE]
                          /\ parked' = [parked EXCEPT ![<<"ne", c>>] = {},
                                                      ![<<"nf", c>>] = {}]
                          /\ toClose' = toClose \ {c}
                          /\ curClose' = c
                     /\ pc' = [pc EXCEPT ![Closer] = "C_notify"]
                ELSE /\ pc' = [pc EXCEPT ![Closer] = "Done"]
                     /\ UNCHANGED << mtx, parked, closed, toClose, curClose >>
          /\ UNCHANGED << buf, recvGen, regs, ready, live, enqueued, received, 
                          released, sendRes, recvRes, selRes, selIdx, selItem, 
                          timedOut, found, scanRes, stack, cv, mx, nc, 
                          toNotify, start, k, staged, first, woke, reg >>

C_notify == /\ pc[Closer] = "C_notify"
            /\ /\ nc' = [nc EXCEPT ![Closer] = curClose]
               /\ stack' = [stack EXCEPT ![Closer] = << [ procedure |->  "notify",
                                                          pc        |->  "C_unlock",
                                                          toNotify  |->  toNotify[Closer],
                                                          nc        |->  nc[Closer] ] >>
                                                      \o stack[Closer]]
            /\ toNotify' = [toNotify EXCEPT ![Closer] = {}]
            /\ pc' = [pc EXCEPT ![Closer] = "N_snapshot"]
            /\ UNCHANGED << mtx, parked, buf, closed, recvGen, regs, ready, 
                            live, enqueued, received, released, sendRes, 
                            recvRes, selRes, selIdx, selItem, timedOut, found, 
                            scanRes, cv, mx, start, k, staged, first, woke, 
                            reg, toClose, curClose >>

C_unlock == /\ pc[Closer] = "C_unlock"
            /\ mtx' = [mtx EXCEPT ![ChM(curClose)] = 0]
            /\ pc' = [pc EXCEPT ![Closer] = "C_loop"]
            /\ UNCHANGED << parked, buf, closed, recvGen, regs, ready, live, 
                            enqueued, received, released, sendRes, recvRes, 
                            selRes, selIdx, selItem, timedOut, found, scanRes, 
                            stack, cv, mx, nc, toNotify, start, k, staged, 
                            first, woke, reg, toClose, curClose >>

closer == C_loop \/ C_notify \/ C_unlock

(* Allow infinite stuttering to prevent deadlock on termination. *)
Terminating == /\ \A self \in ProcSet: pc[self] = "Done"
               /\ UNCHANGED vars

Next == closer
           \/ (\E self \in ProcSet: wait(self) \/ notify(self) \/ scan(self))
           \/ (\E self \in Senders: sender(self))
           \/ (\E self \in Receivers: receiver(self))
           \/ (\E self \in Selectors: selector(self))
           \/ Terminating

Spec == Init /\ [][Next]_vars

Termination == <>(\A self \in ProcSet: pc[self] = "Done")

\* END TRANSLATION
==============================================================================
