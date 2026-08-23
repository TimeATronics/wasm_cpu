;;; Minimal Lisp test for S32 stack machine compiler

(defun fact (n)
  (if (< n 2)
    1
    (* n (fact (- n 1)))))

(defun fib (n)
  (if (< n 2)
    n
    (+ (fib (- n 1)) (fib (- n 2)))))

(define x 42)

(print (fact 5))
(print (fib 10))