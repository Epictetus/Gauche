;;;
;;; digest - abstract base class for message digest algorithms
;;;
;;;  Copyright(C) 2002 by Kimura Fuyuki (fuyuki@hadaly.org)
;;;
;;;  Permission to use, copy, modify, distribute this software and
;;;  accompanying documentation for any purpose is hereby granted,
;;;  provided that existing copyright notices are retained in all
;;;  copies and that this notice is included verbatim in all
;;;  distributions.
;;;  This software is provided as is, without express or implied
;;;  warranty.  In no circumstances the author(s) shall be liable
;;;  for any damages arising out of the use of this software.
;;;
;;;  $Id: digest.scm,v 1.5 2002-12-12 06:32:35 shirok Exp $
;;;

;; An abstract base- and meta-class of message digest algorithms.
;; The concrete implementation is given in modules such as
;; rfc.md5 and rfc.sha1.

(define-module util.digest
  (export <message-digest-algorithm> <message-digest-algorithm-meta>
	  digest-update! digest-final! digest digest-string
          digest-hexify)
  )
(select-module util.digest)

(define-class <message-digest-algorithm-meta> (<class>)
  ())

(define-class <message-digest-algorithm> ()
  ()
  :metaclass <message-digest-algorithm-meta>)

(define-method digest-update! ((self <message-digest-algorithm>) data)
  #f)
(define-method digest-final! ((self <message-digest-algorithm>))
  #f)
(define-method digest ((class <message-digest-algorithm-meta>))
  #f)
(define-method digest-string ((class <message-digest-algorithm-meta>) string)
  (with-input-from-string string (lambda () (digest class))))

;; utility
(define (digest-hexify string)
  (with-string-io string
    (lambda ()
      (port-for-each (lambda (x) (format #t "~2,'0x" x)) read-byte))))

(provide "util/digest")
