;;; Small rect test - 1x12 vertical line at x=64, y=26

(defun draw-pixel (x y)
  (let ((page (/ y 8))
        (bit (% y 8)))
    (let ((addr (+ #x1000 (+ (* page 128) x)))
          (old (peekb addr)))
      (pokeb addr (logor old (<< 1 bit))))))

(defun fill-rect (x y w h)
  (let ((rx x))
    (while (< rx (+ x w))
      (let ((ry y))
        (while (< ry (+ y h))
          (draw-pixel rx ry)
          (set! ry (+ ry 1))))
      (set! rx (+ rx 1)))))

(fill-rect 64 26 1 12)