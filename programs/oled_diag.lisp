;;; Minimal OLED test — fill page 0 entirely with 0xFF (8 top rows all on)
;;; Then fill page 3 with 0xFF (rows 24-31 all on)

;; Fill page 0: 128 bytes of 0xFF
(let ((col 0))
  (while (< col 128)
    (pokeb (+ #x1000 col) #xFF)
    (set! col (+ col 1))))

;; Fill page 3: offset = 3*128 = 384 = #x180
(let ((col 0))
  (while (< col 128)
    (pokeb (+ #x1000 #x180 col) #xFF)
    (set! col (+ col 1))))