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

(defun wtf (question beg end)
  "Prompt the user for a QUESTION and send it to the wtf subprocess.
If the buffer visits a file, it is saved and passed on the command line."
  (interactive "sQuestion: \nr")
  (when buffer-file-name
    (save-buffer))
  (let ((command (if buffer-file-name
                     (list wtf-exe "-file" buffer-file-name question)
                   (list wtf-exe question)))
        (input (buffer-substring-no-properties beg end)))
    (deactivate-mark)
    (eprocs-run
     :name "wtf"
     :buffer "*wtf*"
     :command command
     :input input
     :pipeline (list #'eprocs-filter-ansi-colors))))


(provide 'wtf)
