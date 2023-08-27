
(declare-const a Int)
(declare-const b Int)
(declare-const c Int)

(declare-const u Int)
(declare-const v Int)
(declare-const w Int)

(assert (>= u 0))
(assert (>= v 0))
(assert (>= w 0))

(assert (= a 138600))
(assert (= b 1))
(assert (= c -1))

(define-fun congruent ((x1 Int) (x2 Int) (m Int)) bool
            (= (mod x1 m) (mod x2 m)))

;; u^2 = -bc (mod |a|)
;; v^2 = -ca (mod |b|)
;; w^2 = -ab (mod |c|)

(assert (congruent (* u u) (* (- 0 b) c) a))
(assert (congruent (* v v) (* (- 0 c) a) b))
(assert (congruent (* w w) (* (- 0 a) b) c))

(check-sat)
(get-model)
