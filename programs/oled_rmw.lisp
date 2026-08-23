;;; Manual read-modify-write test for peekb/pokeb

;; Write byte 0x04 at page 2, col 10 (address #x1000 + 2*128 + 10 = #x110A)
(pokeb (+ #x1000 #x10A) #x04)
;; Read it back and OR with 0x08
(let ((old (peekb (+ #x1000 #x10A))))
  (pokeb (+ #x1000 #x10A) (logor old #x08)))