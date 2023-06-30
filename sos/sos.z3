
(declare-const aa Int)
(declare-const bb Int)
(declare-const cc Int)
(declare-const dd Int)
(declare-const ee Int)
(declare-const ff Int)
(declare-const gg Int)
(declare-const hh Int)
(declare-const ii Int)

(declare-const z Int)

(assert (>= aa 0))
(assert (>= bb 0))
(assert (>= cc 0))
(assert (>= dd 0))
(assert (>= ee 0))
(assert (>= ff 0))
(assert (>= gg 0))
(assert (>= hh 0))
(assert (>= ii 0))

(assert (= (+ dd gg) z))
(assert (= (+ bb cc) z))
(assert (= (+ ee ii) z))
(assert (= (+ cc ii) (+ dd ee)))

; (check-sat)
; (get-model)


(define-fun hypothesis () Bool
    (= (+ gg ii) (+ bb ee)))

(assert (not hypothesis))
;; expect unsatisfiable if the hypothesis is valid
(check-sat)
;; or give me model
(get-model)