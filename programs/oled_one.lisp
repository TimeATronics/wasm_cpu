;;; Minimal: set a single pixel at (10, 20) and verify

(let ((page (/ 20 8))
      (bit (% 20 8)))
  (pokeb (+ #x1000 (+ (* page 128) 10))
         (<< 1 bit)))