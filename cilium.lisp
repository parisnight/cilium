;;;; cilium interface  roa 2012.11.25

;;;(asdf:oos 'asdf:load-op :cffi)

(defpackage #:fun
  (:use :cl :cffi))

(in-package :fun)

(defun startit ()
  (define-foreign-library mylib (t (:default "cilium")))
  (use-foreign-library mylib)

  (defcfun ("testcase" %testcase) :void)
  (defcfun ("initdaw" %initdaw) :void)
  (defcfun ("jackframe" %jackframe) :long)
  (defcfun ("pingorpong" %pingorpong) :int)
  (defcfun ("playclip" %playclip) :void
    (clipnum :int)
    (from :int)
    (nframes :int)
    (track :int)
    (to :int))
  (%initdaw)
 )

;;(startit)
;;(%playclip 0 600000 30000 0 0)



;;(%pingorpong)
;;(%jackframe)

(dotimes (i 10) (sleep 1)(print (%pingorpong)))


(defun walk-plist (x fun)
  (when (not (null x))
    (funcall fun (car x) (cadr x))
    (walk-plist (cddr x) fun)))
;;;(walk-plist '(x 5 y 7) #'(lambda(x y) (format t "key ~a value ~a~%" x y)))

(defun array-aref (csym struct pos)
  (mem-aref (foreign-symbol-pointer csym) struct pos))

(defun loadem ()
  (dotimes (i 11)
    (let ((ptr (mem-aref (foreign-symbol-pointer "warray") 'point i)))
      (walk-plist
       '(x 5 y 7 str "/home/fuzz/thisfile/hello-world")
       #'(lambda (x y)
	   (setf (foreign-slot-value ptr 'point x) y))))))
;;;(loadem)

;;;((file sndp vol midich loop keyl keyh vell velh start end))
