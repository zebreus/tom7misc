
;; verify that if a*a + b*b = z
;; then z mod 8 != 3, 6, 7

(declare-const aa Int)
(declare-const bb Int)

(declare-const a Int)
(declare-const b Int)

(declare-const z Int)


(declare-const rz Int)
(declare-const raa Int)
(declare-const rbb Int)

(assert (>= aa 0))
(assert (>= bb 0))
(assert (>= a 0))
(assert (>= b 0))
(assert (>= z 0))


(assert (= (* a a) aa))
(assert (= (* b b) bb))

;; a^2 + b^2 = z
(assert (= (+ aa bb) z))

;; remainder mod 8
(assert (= (mod aa 8) raa))
(assert (= (mod bb 8) rbb))
(assert (= (mod z 8) rz))

;; Z3 doesn't know this fact, so give it a
;; hint
; (assert (or (= raa 1) (= raa 0) (= raa 4)))
; (assert (or (= rbb 1) (= rbb 0) (= rbb 4)))

;; a more basic hint
(assert (if (= (mod a 2) 1)
            (= raa 1)
            (or (= raa 0) (= raa 4))))

(assert (if (= (mod b 2) 1)
            (= rbb 1)
            (or (= rbb 0) (= rbb 4))))

;; 3, 6, 7
; (define-fun hypothesis () Bool
;    (not (or (= rz 3)
;             (= rz 6)
;             (= rz 7))))

;; breaking it into even and odd cases is not interesting
;; because mod 2 is trivially related to mod 8. But anyway...
(define-fun hypothesis () Bool
     (if (= (mod z 2) 1)
         ;; if odd, then one even square and one odd square
         (or (= rz 1) (= rz 5))
         ;; if even, then two even or two odd
         (or (= rz 0) (= rz 2) (= rz 4))
         ))

(assert (not hypothesis))
;; expect unsatisfiable if the hypothesis is valid
(check-sat)
;; or give me model
(get-model)

