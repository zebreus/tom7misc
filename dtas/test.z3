
;; try modeling a toy program

(define-sort Byte () (_ BitVec 8))
(define-sort Memory () (Array (_ BitVec 11) Byte))
;; say we have registers A, B, C
(declare-datatypes () ((State (state (mem Memory) (reg-pc Byte) (reg-a Byte) (reg-b Byte) (reg-c Byte)))))

(declare-const initial-memory Memory)
(declare-const initial-state State)

;; assert initial values
;; bv0 is the value 0 in this bizarre syntax, of bit length 11.
;; when you're working with bytes, you can just write #x00.
(assert (= (select initial-memory (_ bv0 11)) #xff))

(assert (= initial-state (state initial-memory #x00 #x00 #x00 #x00)))

;; we say now that there is a function from time to a machine state.
; (declare-fun trace (Int) State)
; (assert (= (trace 0) initial-state))
(declare-const trace (Array Int State))
(assert (= (select trace 0) initial-state))

; (assert
;  (forall
;   ((trace-idx Int))
;   (implies (<= trace-idx 0) (= (trace trace-idx) initial-state))))
;
;; the index determines

;; instruction 0: read memory at location 0 in to the register a.
;; (declare-const memory0 Memory)


;; now we give the graph.

(assert
 (forall
  ((trace-idx Int))
  (implies (and (> trace-idx 0) (< trace-idx 50))
  (let ((state-in (select trace trace-idx))
        (state-out (select trace (+ trace-idx 1))))
    (let ((pc (reg-pc state-in)))
      ;; assume every instruction is a no-op
      (= state-out (state (mem state-in) (bvadd pc #x01)
                          (reg-a state-in) (reg-b state-in) (reg-c state-in)))
      )))
 ))


(check-sat)
(get-model)
