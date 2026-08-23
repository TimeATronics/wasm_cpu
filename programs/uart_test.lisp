;;; CPU UART test — prints "OK\r\n" via MMIO UART TX
;;; UART TX data register: byte address #xFE000000

(poke #xFE000000 #x4F)   ;; 'O'
(poke #xFE000000 #x4B)   ;; 'K'
(poke #xFE000000 #x0D)   ;; '\r'
(poke #xFE000000 #x0A)   ;; '\n'

;; Done — halt to turn on all LEDs
(poke #xFFF00000 #x00)