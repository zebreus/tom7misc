;;    -*- mode: emacs-lisp; coding: utf-8; lexical-binding: t; -*-

;; Takes a file path (with optional :line:col) like from an error
;; message, attempting to extract one from the copy-paste buffer
;; as the default if possible. Opens that file if it can be found
;; by matching parents of the current file.

(defun fff-resolve-path-upwards (rel-path)
  "Walk up from `default-directory` looking for existing file REL-PATH."
  (if (and (file-name-absolute-p rel-path) (file-regular-p rel-path))
      rel-path
    (let ((dir (expand-file-name default-directory))
          (found nil))
      (while (and dir (not found))
        (let ((candidate (expand-file-name rel-path dir)))
          (if (file-regular-p candidate)
              (setq found candidate)
            (let ((parent (file-name-directory (directory-file-name dir))))
              (setq dir (if (or (null parent) (equal parent dir))
                            nil
                          parent))))))
      found)))

(defun fff-parse-error-string (str)
  "Find a file path in STR matching our current directory hierarchy.
Returns (FILE LINE COL) if an existing file is found, or nil."
  (when (and str (not (string-empty-p str)))
    (let ((pos 0)
          (found nil)
          ;; Match path-like tokens with optional :line and :col suffixes.
          (token-re (concat "\\([a-zA-Z0-9_./~-]+\\)"
                            "\\(?::\\([0-9]+\\)\\(?::\\([0-9]+\\)\\)?\\)?")))
      (while (and (not found) (string-match token-re str pos))
        (setq pos (match-end 0))
        (let* ((raw-path (match-string 1 str))
               (line-str (match-string 2 str))
               (col-str (match-string 3 str))
               ;; Strip leading/trailing punctuation like quotes or parens
               (clean-path (replace-regexp-in-string
                            "\\`[:.,;]+\\|[:.,;]+\\'" "" raw-path))
               ;; Also try stripping a leading a/ or b/ from git diff paths
               (alt-path (and (string-match "\\`[ab]/\\(.+\\)" clean-path)
                              (match-string 1 clean-path)))
               (resolved (and (not (string-empty-p clean-path))
                              (or (fff-resolve-path-upwards clean-path)
                                  (and alt-path
                                       (fff-resolve-path-upwards alt-path))))))
          (when resolved
            (setq found (list resolved
                              (and line-str (string-to-number line-str))
                              (and col-str (string-to-number col-str)))))))
      found)))

(defun fff-selection-candidates ()
  "Return copy/paste strings in preference order (PRIMARY, CLIPBOARD,
SECONDARY, kill-ring), sorting X selections by TIMESTAMP when available."
  (let ((entries nil)
        (rank 0))
    (dolist (sel '(PRIMARY CLIPBOARD SECONDARY))
      (setq rank (1+ rank))
      (let ((text (or (ignore-errors (gui-get-selection sel 'UTF8_STRING))
                      (ignore-errors (gui-get-selection sel 'STRING))))
            (ts (ignore-errors (gui-get-selection sel 'TIMESTAMP))))
        (when (and (stringp text) (not (string-empty-p (string-trim text))))
          (push (list (string-trim (substring-no-properties text))
                      (if (numberp ts) ts -1)
                      rank)
                entries))))
    (setq entries
          (sort (nreverse entries)
                (lambda (a b)
                  (let ((ts-a (nth 1 a))
                        (ts-b (nth 1 b)))
                    (if (and (> ts-a 0) (> ts-b 0) (/= ts-a ts-b))
                        (> ts-a ts-b)
                      (< (nth 2 a) (nth 2 b)))))))
    (let ((kill (ignore-errors (current-kill 0 t)))
          (result (mapcar #'car entries)))
      (when (and (stringp kill) (not (string-empty-p (string-trim kill))))
        (setq result
              (append result
                      (list (string-trim (substring-no-properties kill))))))
      (delete-dups result))))

(defun do-fancy-find-file (str)
  "Find a file path in STR matching our current directory hierarchy and open it.
Defaults to the first selection / kill-ring entry that resolves to an
existing file. If `:LINE` (or `:LINE:COL`) follows the filename, jump there."
  (interactive
   (let* ((candidates (fff-selection-candidates))
          (default (or (seq-find #'fff-parse-error-string candidates)
                       (car candidates)))
          (prompt (if (and default (not (string-empty-p default)))
                      (format "Error string (default %s): "
                              (truncate-string-to-width
                               default 40 nil nil "..."))
                    "Error string: ")))
     (list (read-string prompt nil nil default))))
  (let ((parsed (fff-parse-error-string str)))
    (if (not parsed)
        (user-error "No existing file found in: %s" str)
      (let ((found-file (nth 0 parsed))
            (found-line (nth 1 parsed))
            (found-col (nth 2 parsed)))
        (find-file found-file)
        (when found-line
          (goto-char (point-min))
          (forward-line (1- found-line))
          (when (and found-col (> found-col 0))
            (move-to-column (1- found-col))))))))

(global-set-key (kbd "C-x g") #'do-fancy-find-file)

(provide 'fancy-find-file)
