
;; TODO!
;; Add help text.

(defvar select-buffers-mode-map (make-sparse-keymap)
  "Keymap for select-buffers-mode.")

(defvar select-buffers-priority-buffers
  '("style-guide.txt")
  "A list of buffer names that should be ordered first.")

(define-minor-mode select-buffers-mode
  "Minor mode for selecting buffers."
  :lighter " Buffers"
  (if select-buffers-mode
      (progn
        (use-local-map select-buffers-mode-map))
    (progn
      (use-local-map nil))
    ))

(defvar select-buffers-selected-face
  '(:background "#009" :foreground "#CCF")
  "Face used to highlight selected lines in select-buffers-mode.")

(defvar select-buffers-priority-face
  '(:foreground "#ffc")
  "Face used to highlight priority lines in select-buffers-mode.")

(defvar select-buffers-highlight-overlays nil
  "A list of overlays to keep track of highlights.")


(defun select-buffers-redraw ()
  (let ((old-bro buffer-read-only))
    (when old-bro (setq buffer-read-only nil))
    (erase-buffer)
    (insert
     (mapconcat
      (lambda (buffer)
        (let* ((buffer-name (buffer-name buffer))
               (checkbox (if (member buffer select-buffers-selected-buffers) "[x]" "[ ]"))
               (base-face (if (member buffer select-buffers-selected-buffers)
                              select-buffers-selected-face
                            nil))
               (merged-face
                (if (member buffer-name select-buffers-priority-buffers)
                    (face-remap-add-relative select-buffers-priority-face base-face)
                  base-face)))
          (propertize
           (format "%s %s" checkbox buffer-name)
           'face merged-face)))
      select-buffers-buffer-list
      "\n"))
    (when old-bro (setq buffer-read-only t))
    ))

(defun select-buffers ()
  (interactive)
  (let ((buffer (generate-new-buffer "*Buffer Selector*")))
    (with-current-buffer buffer
      (setq select-buffers-selected-buffers nil)
      (select-buffers-refresh)
      (goto-char (point-min))
      (set-buffer-modified-p nil)
      (setq buffer-read-only t)
      ; Enable the minor mode
      (select-buffers-mode 1)
      (switch-to-buffer buffer)
      (setq mode-line-format
            (list " "
                  (propertize "Buffers" 'face 'mode-line-buffer-id)
                  " "
                  mode-line-position
                  " "
                  mode-line-modified
                  " "
                  mode-line-modes))
      )
    select-buffers-selected-buffers))

(defun select-buffers-toggle-checkbox ()
  (interactive)
  (let* ((current-line (line-number-at-pos))
         (buffer (nth (1- current-line) select-buffers-buffer-list)))
    (if (member buffer select-buffers-selected-buffers)
        (setq select-buffers-selected-buffers
              (remove buffer select-buffers-selected-buffers))
      (push buffer select-buffers-selected-buffers))
    (select-buffers-redraw)
    (goto-char (point-min))
    ;; go back where we were, and then one more
    (forward-line current-line)))

(defun select-buffers-collect-and-sort ()
  (let* ((dependency-map (select-buffers-build-dependency-map select-buffers-selected-buffers))
         (orig-sorted-buffer-names (select-buffers-topological-sort dependency-map))
         ;; Only consider the ones that we will output, or else it can interfere with
         ;; reordering here.
         (filtered-buffer-names (remove-if-not
                                 (lambda (name)
                                   (member (get-buffer name) select-buffers-selected-buffers))
                                 orig-sorted-buffer-names))
         (sorted-buffer-names (select-buffers-reorder-buffer-names filtered-buffer-names dependency-map))
         (sorted-buffers (mapcar (lambda (name) (get-buffer name)) sorted-buffer-names))
         (buffer-contents
          (mapcar (lambda (buffer)
                    (cons (buffer-name buffer)
                          (with-current-buffer buffer (buffer-string))))
                  sorted-buffers)))
    (message "Dependency Map: %s" dependency-map)
    (message "Sorted Buffer Names: %s" sorted-buffer-names)
    (select-buffers-create-output-buffer buffer-contents)
    buffer-contents))

(defun select-buffers-create-output-buffer (sorted-contents)
  (let ((output-buffer (generate-new-buffer "*Combined Buffers*")))
    (with-current-buffer output-buffer
      (dolist (pair sorted-contents)
        (insert (format "--- %s ---\n" (car pair)))
        (insert (cdr pair))
        (insert "\n\n"))
      (goto-char (point-min))
      (switch-to-buffer output-buffer))))

(defun select-buffers-exit ()
  (interactive)
  (select-buffers-collect-and-sort)
  ; (kill-buffer (current-buffer))
  )


(defun select-buffers-extract-includes (buffer-content)
  (let ((includes nil))
    (with-temp-buffer
      (insert buffer-content)
      (goto-char (point-min))
      (while (re-search-forward "^#include \"\\([^\"]+\\)\"" nil t)
        (push (match-string 1) includes)))
    includes))

(defun select-buffers-build-dependency-map (buffer-list)
  (let ((dependency-map (make-hash-table :test 'equal)))
    (dolist (buffer buffer-list)
      (let ((buffer-name (buffer-name buffer))
            (includes (select-buffers-extract-includes (with-current-buffer buffer (buffer-string)))))
        (puthash buffer-name includes dependency-map)))
    dependency-map))

;; helper function for below.
;; remaining-input: A list of buffer names remaining to be processed.
;; current-base-name: The base name of the current header file.
;; will-skip: Files that will be skipped if we move the source file up; should be empty
;;   for the initial call.
;; dependency-map: A hash table mapping buffer names to their dependencies.
;; Returns a cons cell where the car is a list of buffer names to push onto the output list
;; (either zero or one source file), and the cdr is the new filtered input list. The header
;; file is not included.
(defun find-and-reorder-source (current-base-name remaining-input will-skip dependency-map)
  (if (null remaining-input)
      ;; for the empty list
      (cons nil nil)
    ;; otherwise, is the next file a source file for the base name?
    (let ((file (car remaining-input))
          (rest (cdr remaining-input)))
      (message (format "  try %s vs %s" file current-base-name))
      (if (string-equal file (concat current-base-name ".cc"))
          ;; Found. Is it safe to move up?
          (let ((source-deps (gethash file dependency-map)))
            (if (some (lambda (dep) (member dep will-skip)) source-deps)
                (progn
                  (message "    Skipping reordering %s after %s due to dependency"
                           file current-base-name)
                  (cons nil remaining-input))
              (progn
                (message "    Reordering %s after %s" file current-base-name)
                (cons (list file) rest))))

        ;; otherwise, recurse with this file added to will-skip, and
        ;; then put this file back on the filtered list.
        (let* ((rec (find-and-reorder-source
                     current-base-name rest (cons file will-skip) dependency-map))
               (hrec (car rec))
               (trec (cdr rec)))
          (cons hrec (cons file trec)))
        ))))

(defun select-buffers-reorder-buffer-names (buffer-names dependency-map)
  (message "Starting select-buffers-reorder-buffer-names with buffer-names: %s" buffer-names)
  (let ((priority-buffers nil)
        (remaining-buffers nil))
    ;; first, put priority buffers first.
    (dolist (name buffer-names)
      (if (member name select-buffers-priority-buffers)
          (push name priority-buffers)
        (push name remaining-buffers)))
    (setq buffer-names (append (reverse priority-buffers) (reverse remaining-buffers)))
    ;; now reorder based on dependency heuristics.
    (labels ((reorder-recursive (input-list output-list)
               (cond
                ((null input-list)
                 (message "  Input list is nil, returning: %s" (reverse output-list))
                 (reverse output-list))
                (t
                 (let* ((current-name (car input-list))
                        (remaining-input (cdr input-list))
                        (base-name (replace-regexp-in-string "\\.\\(h\\|hpp\\)$" "" current-name))
                        (is-header (or (string-suffix-p ".h" current-name) (string-suffix-p ".hpp" current-name))))
                   (message "  Processing current-name: %s, base-name: %s, is-header: %S"
                            current-name base-name is-header)
                   (cond
                    (is-header
                     ;; If it's a header, look for the corresponding source file
                     (let* ((result (find-and-reorder-source base-name remaining-input nil dependency-map))
                            (buffers-to-add (car result))
                            (new-remaining-input (cdr result)))
                       (reorder-recursive new-remaining-input (append (append buffers-to-add (list current-name)) output-list))))
                    (t
                     ;; If it's not a header, just add it to the output list
                     (reorder-recursive remaining-input (cons current-name output-list)))))))))
      (reorder-recursive buffer-names nil))))

(defun select-buffers-topological-sort (dependency-map)
  (let ((visited (make-hash-table :test 'equal))
        (stack nil)
        (sorted-list nil)
        (cycle-detected nil))
    (labels ((visit (node)
               (cond
                ((gethash node visited)
                 (when (eq (gethash node visited) 'visiting)
                   (setq cycle-detected t)
                   (message "Warning: Circular dependency detected involving %s" node))
                 nil) ; Return nil if already visited
                (t
                 (puthash node 'visiting visited)
                 (dolist (dep (gethash node dependency-map))
                   (visit dep))
                 (puthash node 'visited visited)
                 (push node sorted-list)
                 t)))) ; Return t if successfully visited
      (maphash (lambda (node _) (visit node)) dependency-map)
      (if cycle-detected
          (message "Warning: Output may not be in the desired order due to circular dependencies.")
        (setq sorted-list (reverse sorted-list)))
      sorted-list)))

(defun select-buffers-refresh ()
  (interactive)
  (let* ((selected-names (mapcar 'buffer-name select-buffers-selected-buffers))
         (new-buf-list (buffer-list))
         (filtered-buf-list
          (cl-remove-if
           (lambda (buffer)
             ;; exclude *Special Buffers*
             ;; even if they have <disamb> at the end.
             (string-match "^[ ]*\\*.*\\*\\(?:<.*>\\)?$" (buffer-name buffer)))
           new-buf-list)))
    (setq select-buffers-buffer-list filtered-buf-list)
    (setq select-buffers-selected-buffers
          (cl-remove-if-not
           (lambda (buffer) (member (buffer-name buffer) selected-names))
           filtered-buf-list))
    (select-buffers-redraw)
    (goto-char (point-min))))

(define-key select-buffers-mode-map (kbd "q") 'select-buffers-exit)
(define-key select-buffers-mode-map (kbd "<SPC>") 'select-buffers-toggle-checkbox)
(define-key select-buffers-mode-map (kbd "<return>") 'select-buffers-exit)
(define-key select-buffers-mode-map (kbd "<next>") 'forward-line)
(define-key select-buffers-mode-map (kbd "<prior>") 'backward-line)
(define-key select-buffers-mode-map (kbd "g") 'select-buffers-refresh)

(provide 'select-buffers)
