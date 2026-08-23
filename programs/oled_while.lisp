;;; Test while loop: increment counter 5 times, then poke the counter

(let ((cnt 0))
  (while (< cnt 5)
    (set! cnt (+ cnt 1)))
  (pokeb (+ #x1000 0) cnt))