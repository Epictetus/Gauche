;;;
;;;  gauche.experimenta.lamb - shorthand notation of lambda
;;;

(define-module gauche.experimental.lamb
  (use util.match)
  (export ^ ^. ^* ^_))
(select-module gauche.experimental.lamb)

(define-syntax ^
  (syntax-rules ()
    [(^ formals . body) (lambda formals . body)]))

(define-syntax ^.
  (syntax-rules ()
    [(^. . clauses) (match-lambda . clauses)]))

(define-syntax ^*
  (syntax-rules ()
    [(^* . clauses) (match-lambda* . clauses)]))

(define-macro (^_ . body)
  `(lambda (_) ,@body))

(provide "gauche/experimental/lamb")
