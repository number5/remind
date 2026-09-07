;;; -*- lexical-binding: t -*-
;;; setup for remind autocompletion (totally optional)

;; put something like 
;; (add-hook 'remind-conf-mode-hook (lambda () (load-library "ac-remind")))
;; In your .emacs file

(require 'auto-complete)

(add-to-list 'ac-dictionary-directories' "~/.emacs.d/ac-dict")
(add-to-list 'ac-user-dictionary-files "~/.emacs.d/ac-dict/remind-conf-mode")
(ac-clear-dictionary-cache)
(make-local-variable 'ac-sources)

(setq ac-sources '(ac-source-dictionary
                  ac-source-words-in-same-mod-buffers))
(setq auto-complete-mode 1)

(provide 'ac-remind)

