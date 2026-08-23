;;; Test pokeb directly with print

(pokeb (+ #x1000 266) #xAB)
(print (peekb (+ #x1000 266)))