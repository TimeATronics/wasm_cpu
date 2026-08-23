;;; OLED framebuffer test

;;; Helper: fill a byte at given page and column
(defun fb-poke (page col byte)
  (pokeb (+ #x1000 (+ (* page 128) col)) byte))

;;; Helper: read a byte from framebuffer
(defun fb-peek (page col)
  (peekb (+ #x1000 (+ (* page 128) col))))

;;; Set a single pixel by bit-masking
(defun draw-pixel (x y)
  (let ((page (/ y 8))
        (bit (% y 8)))
    (let ((addr (+ #x1000 (+ (* page 128) x)))
          (old (peekb addr)))
      (pokeb addr (logor old (<< 1 bit))))))

;;; Fill a filled rectangle
(defun fill-rect (x y w h)
  (let ((rx x))
    (while (< rx (+ x w))
      (let ((ry y))
        (while (< ry (+ y h))
          (draw-pixel rx ry)
          (set! ry (+ ry 1))))
      (set! rx (+ rx 1)))))

;;; Fill entire screen with horizontal lines
(defun h-lines ()
  (let ((y 0))
    (while (< y 64)
      (let ((page (/ y 8))
            (bit (% y 8))
            (val (<< 1 bit)))
        (let ((col 0))
          (while (< col 128)
            (pokeb (+ #x1000 (+ (* page 128) col)) val)
            (set! col (+ col 1)))))
      (set! y (+ y 2)))))

;;; Draw a border box
(defun draw-box ()
  (fill-rect 0 0 128 1)
  (fill-rect 0 63 128 1)
  (fill-rect 0 0 1 64)
  (fill-rect 127 0 1 64)
  (fill-rect 30 22 68 20)
  (fill-rect 64 26 1 12))

;;; Write "WASM" using filled rectangles
(defun draw-text ()
  ;; W — two vertical bars + stepped diagonals forming a V
  (fill-rect 10 20 4 24)    ; left bar
  (fill-rect 26 20 4 24)    ; right bar
  ;; left down-diagonal (from x=14 at y=20 down-right to x=20 at y=40)
  (fill-rect 14 24 4 4)
  (fill-rect 15 28 4 4)
  (fill-rect 16 32 4 4)
  (fill-rect 17 36 4 4)
  (fill-rect 18 40 4 4)
  ;; right down-diagonal (from x=22 at y=20 down-left to x=16 at y=40)
  (fill-rect 22 24 4 4)
  (fill-rect 21 28 4 4)
  (fill-rect 20 32 4 4)
  (fill-rect 19 36 4 4)
  (fill-rect 18 40 4 4)
  ;; A — left/right bars + top/middle horizontal
  (fill-rect 34 20 4 24)
  (fill-rect 34 20 12 4)
  (fill-rect 34 30 12 4)
  (fill-rect 42 20 4 24)
  ;; S — top/middle/bottom horizontals + two vertical segments
  (fill-rect 54 20 16 4)
  (fill-rect 54 20 4 12)
  (fill-rect 54 30 16 4)
  (fill-rect 66 30 4 12)
  (fill-rect 54 40 16 4)
  ;; M — two vertical bars + stepped diagonals meeting in center
  (fill-rect 76 20 4 24)    ; left bar
  (fill-rect 92 20 4 24)    ; right bar
  ;; left down-diagonal (from x=80 to x=86 at bottom)
  (fill-rect 80 24 4 4)
  (fill-rect 81 28 4 4)
  (fill-rect 82 32 4 4)
  (fill-rect 83 36 4 4)
  (fill-rect 84 40 4 4)
  ;; right down-diagonal (from x=88 to x=82 at bottom)
  (fill-rect 88 24 4 4)
  (fill-rect 87 28 4 4)
  (fill-rect 86 32 4 4)
  (fill-rect 85 36 4 4)
  (fill-rect 84 40 4 4))

(draw-text)