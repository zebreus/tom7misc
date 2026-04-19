;;;       -*- lexical-binding: t -*-
;;; Lexical binding is required!

;; Prompts the user for a question.
;; If the buffer has an associated filename, saves the buffer
;; and passes the filename and question on the command line
;; to a subprocess.
;;
;; The subprocess runs asynchronously using the support code
;; in eprocs.el (see fill-in.el for example usage) and prints
;; its output into a new buffer with ANSI color, etc.
;;
;; The current buffer is not modified. The region is unhighlighted
;; since we have completed the action on it (e.g. following the UX
;; of commands like M-w).

(require 'eprocs)

(defvar wtf-exe "c:\\code\\sf_svn\\httpc\\wtf.exe"
  "The executable to use for the wtf command. It will be invoked like
  wtf.exe -file current_file \"This is the user's question.\"
  with the contents of the region as stdin.")

(defcustom wtf-configs
  '("/c/code/sf_svn/sitelisp/model-config.txt"
    ; ...
    )
  "List of config files passed to wtf."
  :type '(repeat file)
  :group 'wtf)

(defun wtf (question beg end)
  "Prompt the user for a QUESTION and send it to the wtf subprocess.
If the buffer visits a file, it is saved and passed on the command line."
  (interactive "sQuestion: \nr")
  ;; Grab the selected region before we do anything else, so that
  ;; something like a save hook doesn't wreck it.
  (let ((input (buffer-substring-no-properties beg end))
        (nonce (format "%06x" (random #xffffff))))

    (when buffer-file-name
      (save-buffer))
    (deactivate-mark)

    (let* ((wtf-command
            (append (list wtf-exe)
                    (if buffer-file-name
                        (list "-file" buffer-file-name)
                      nil)
                    (apply #'append
                           (mapcar (lambda (cfg) (list "-config" cfg))
                                   fill-in-configs))
                    (list question))))
      (eprocs-run
       :name (format "wtf-%s" nonce)
       :buffer (format "*wtf-%s*" nonce)
       :command wtf-command
       :input input
       :pipeline (list #'eprocs-filter-ansi-colors)))))


(provide 'wtf)
