;;; LED blink test — writes to MMIO LED register at #xFFF00000
;;; LEDs: bit 0 = LED5, ..., bit 5 = LED0 (active low: 0=on, 1=off)

(let ((i 0))
  (while (= 1 1)
    (poke #xFFF00000 #x00)   ;; all LEDs on
    (let ((d 0))
      (while (< d 500000)
        (set! d (+ d 1))))
    (poke #xFFF00000 #x3F)   ;; all LEDs off
    (let ((d 0))
      (while (< d 500000)
        (set! d (+ d 1))))))