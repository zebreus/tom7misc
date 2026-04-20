
;; this is typically bound to C-c c

(defun cleanup ()
  "Apply cleanups defined in the *Cleanups* buffer."
  (interactive)
  (let ((cleanup-buffer-name "*Cleanups*"))
    (if-let ((conf-buf (get-buffer cleanup-buffer-name)))
        ;; Buffer exists: parse and apply rules.
        (let ((insertion-point (point))
              (records (with-current-buffer conf-buf
                         (cleanup--parse-records))))
          (dolist (record records)
            (let ((pattern (car record))
                  (action (cdr record)))
              (when (and pattern action (cleanup--pattern-exists-p pattern))
                (if (string-prefix-p "+" action)
                    ;; Insertion action
                    (let ((text-to-insert (substring action 1)))
                      (unless (cleanup--line-exists-p text-to-insert)
                        (save-excursion
                          (goto-char insertion-point)
                          (insert text-to-insert "\n")
                          (setq insertion-point (point))
                          )))
                  ;; Replacement action
                  (save-excursion
                    (goto-char (point-min))
                    (while (re-search-forward pattern nil t)
                      (replace-match action nil t))))))))
      ;; Buffer does not exist: create and switch.
      (let ((buf (get-buffer-create cleanup-buffer-name)))
        (switch-to-buffer buf)
        (cleanup-mode)
        (insert "; The *Cleanup* buffer configures the cleanup command\n"
                "; as a series of lines. A line like:\n"
                ";   pattern<TAB>+#include <cstdint>\n"
                "; inserts the include when the pattern matches\n"
                "; anywhere in the file. Without +, the line is a\n"
                "; regex replacement:\n"
                ";   uint\\([0-9]+\\)\\([^_][^t]\\)<TAB>uint\\1_t\\2\n")
        ))
    ))

(defun cleanup--parse-records ()
  "Parse current buffer into a list of (PATTERN . ACTION) cons cells."
  (require 'cl-lib)
  (let ((lines (split-string (buffer-string) "\n" t)))
    (cl-loop for line in lines
             unless (string-prefix-p ";" line)
             collect (let ((parts (split-string line "\t" t)))
                       (cons (car parts) (cadr parts))))))

(defun cleanup--pattern-exists-p (pattern)
  "Return non-nil if PATTERN exists in the current buffer."
  (save-excursion
    (goto-char (point-min))
    (re-search-forward pattern nil t)))

(defun cleanup--line-exists-p (line-text)
  "Return non-nil if LINE-TEXT exists as a whole line in the current buffer."
  (save-excursion
    (goto-char (point-min))
    (re-search-forward (concat "^" (regexp-quote line-text) "$") nil t)))

(defgroup cleanup nil
  "Custom cleanup command settings."
  :group 'applications)

(defface cleanup-pattern-face
  '((t :inherit font-lock-function-name-face))
  "Face for the pattern part of a cleanup rule."
  :group 'cleanup)

(defface cleanup-tab-face
  '((t :inherit font-lock-comment-delimiter-face))
  "Face for the tab separator in a cleanup rule."
  :group 'cleanup)

(defface cleanup-action-face
  '((t :inherit font-lock-string-face))
  "Face for the action part of a cleanup rule."
  :group 'cleanup)

(defface cleanup-action-command-face
  '((t :inherit font-lock-keyword-face))
  "Face for the command in an action, like '+'."
  :group 'cleanup)

(defface cleanup-comment-face
  '((t :inherit font-lock-comment-face))
  "Face for comments in a cleanup rule file."
  :group 'cleanup)

(defvar cleanup-font-lock-keywords
  (list
   '("^;.*$" 0 'cleanup-comment-face)

   ;; Rule for insertion actions: PATTERN<tab>+ACTION
   '("^\\([^\t]+\\)\\(\t\\)\\(\\+\\)\\(.*\\)$"
     (1 'cleanup-pattern-face)
     (2 'cleanup-tab-face)
     (3 'cleanup-action-command-face)
     (4 'cleanup-action-face))

   ;; Rule for replacement actions: PATTERN<tab>ACTION
   '("^\\([^\t]+\\)\\(\t\\)\\(.*\\)$"
     (1 'cleanup-pattern-face)
     (2 'cleanup-tab-face)
     (3 'cleanup-action-face)))
  "Font lock keywords for cleanup-mode.")

(define-derived-mode cleanup-mode fundamental-mode "Cleanup"
  "Major mode for editing *Cleanups* buffer."
  (setq-local font-lock-defaults '(cleanup-font-lock-keywords)))

(provide 'cleanup)
