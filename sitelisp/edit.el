;;;       -*- lexical-binding: t -*-
;;; Lexical binding is required!

;; Runs an external utility "edit.exe" get LLM-driven edits,
;; and interactively applies them.

; Pro-tip: Use M-x toggle-debug-on-error

;; TODO:
;;  - Better diff display!
;;  - Improve colors in header line
;;  - Be smarter when we invoke llm-edit while there is already an
;;    *EDITS* buffer with pending edits. We should add to this buffer,
;;    but also handle the case that the source files are not in the
;;    same default directory. Probably we should rewrite existing
;;    File: lines so that they are correct for the new default directory;
;;    this would keep the filenames short in the most common case that
;;    we handle every edit before starting a new one.



(require 'eprocs)

(defgroup edit nil
  "Settings for edit command integration."
  :group 'external)

(defcustom edit-exe "c:\\code\\sf_svn\\httpc\\edit.exe"
  "The executable to use for the edit command."
  :type 'file
  :group 'edit)

(defcustom edit-configs
  '("c:\\code\\sf_svn\\sitelisp\\model-config.txt"
    ; ...
    )
  "List of config files passed to llm-edit."
  :type '(repeat file)
  :group 'edit)

(require 'cl-lib)
(require 'diff-mode)

(defface llm-edit-before-face
  '((t (:extend t :background "#332222")))
  "Face for the before region lines in the edit buffer."
  :group 'edit)

(defface llm-edit-after-face
  '((t (:extend t :background "#223322")))
  "Face for the after region lines in the edit buffer."
  :group 'edit)

(defface llm-edit-delimiter-face
  '((t (:extend t :background "#222255" :foreground "#8dd")))
  "Face for the delimiter lines (<<<< BEFORE, etc.)."
  :group 'edit)

(defface llm-edit-file-face
  '((t (:foreground "#8af")))
  "Face for the filename in an edit block."
  :group 'edit)

(defvar-local llm-edit--active-clone-info nil
  "Holds (CLONE-BUF TARGET-BUF) for the currently active diff preview.")
(defvar-local llm-edit--last-block-state nil)

(defun llm-edit--cleanup-preview ()
  "Discard the clone and restore the target buffer in the preview window."
  (when llm-edit--active-clone-info
    (cl-destructuring-bind (clone-buf target-buf) llm-edit--active-clone-info
      (let ((win (get-buffer-window clone-buf)))
        (when (and win (buffer-live-p target-buf))
          (let ((pt (window-point win))
                (start (window-start win)))
            (set-window-buffer win target-buf)
            (set-window-point win pt)
            (set-window-start win start))))
      (when (buffer-live-p clone-buf)
        (kill-buffer clone-buf)))
    (setq llm-edit--active-clone-info nil)))

(defun llm-edit--count-remaining ()
  "Count the remaining edit blocks in the buffer."
  (save-excursion
    (save-match-data
      (goto-char (point-min))
      (let ((count 0))
        (while (re-search-forward "^File: " nil t)
          (setq count (1+ count)))
        count))))

;; Inserts a formatted diff block into the current buffer at point.
;; Crucially, applies read-only and rear-nonsticky text properties to the
;; structural delimiters (like "<<<< BEFORE"). This prevents the user from
;; accidentally corrupting the boundaries while tweaking the proposed edits,
;; ensuring the parser won't break later.
(defun llm-edit--insert-diff (file comment before after)
  "Format and insert the edit block into the current buffer at point."
  (let ((inhibit-read-only t))
    (insert
     (propertize "File: "
                 'read-only t 'rear-nonsticky t
                 'font-lock-face 'font-lock-keyword-face))
    (insert
     (propertize (format "%s\n" file)
                 'read-only t 'rear-nonsticky t
                 'font-lock-face 'llm-edit-file-face))
    (insert
     (propertize "Comment: "
                 'read-only t 'rear-nonsticky t
                 'font-lock-face 'font-lock-keyword-face))
    (insert
     (propertize (format "%s\n" comment)
                 'read-only t 'rear-nonsticky t
                 'font-lock-face 'font-lock-comment-face))
    (insert
     (propertize "<<<< BEFORE\n"
                 'read-only t 'rear-nonsticky t
                 'font-lock-face 'llm-edit-delimiter-face))
    (insert
     (propertize before 'font-lock-face 'llm-edit-before-face))
    (insert
     (propertize "==== AFTER\n"
                 'read-only t 'rear-nonsticky t
                 'font-lock-face 'llm-edit-delimiter-face))
    (insert
     (propertize after 'font-lock-face 'llm-edit-after-face))
    (insert
     (propertize ">>>>\n"
                 'read-only t 'rear-nonsticky t
                 'font-lock-face 'llm-edit-delimiter-face))
    (insert
     (propertize "\n"
                 'read-only t 'rear-nonsticky t))))

;; Processes a JSON payload received from the external edit process.
;; Extracts the file, comment, and before/after text blocks, formatting
;; them into a structured representation in the *EDITS* buffer.
;;
;; Normalizes CRLF to LF to prevent text-matching issues on Windows.
(defun llm-edit--process-json (payload)
  "Parse the JSON payload and append formatted block to *EDITS*."
  (condition-case err
      (let* ((json (json-parse-string payload))
             (file (gethash "file" json))
             (comment (gethash "comment" json))
             (raw-before (gethash "before" json))
             (raw-after (gethash "after" json)))
        (when (and file raw-before raw-after)
          (let ((before (replace-regexp-in-string "\r\n" "\n" raw-before))
                (after (replace-regexp-in-string "\r\n" "\n" raw-after)))
            (unless (string-suffix-p "\n" before)
              (setq before (concat before "\n")))
            (unless (string-suffix-p "\n" after)
              (setq after (concat after "\n")))
            (with-current-buffer (get-buffer "*EDITS*")
              (save-excursion
                (goto-char (point-max))
                (llm-edit--insert-diff file comment before after))))))
    (error (message "llm-edit JSON parse error: %s" err))))

;; Extracts the details of the edit block surrounding the current point
;; in the *EDITS* buffer. Returns a list containing the file, comment,
;; before-text, after-text, start, and end positions.
;;
;; Scans backwards to find the start of the block and forwards for the end,
;; validating that the cursor is strictly within these bounds. Uses
;; buffer-substring-no-properties to strip away the read-only and other
;; formatting properties so the plain text can be reliably searched and
;; replaced in the target buffer.
(defun llm-edit--parse-current-block ()
  "Parse the edit block at point. Returns a list of (file, comment,
   before, after, start, end)."
  (let ((orig-point (point)))
    (save-excursion
      (let ((start (save-excursion
                     (goto-char (line-beginning-position))
                     (if (looking-at "File: ")
                         (point)
                       (re-search-backward "^File: " nil t)))))
        (when start
          (goto-char start)
          (let ((end (save-excursion
                       (when (re-search-forward "^>>>>\\(\n\\|$\\)" nil t)
                         (match-end 0)))))
            (when (and end (<= start orig-point) (<= orig-point end))
              (let ((file (when (looking-at "^File: \\(.*\\)$")
                            (match-string-no-properties 1)))
                    comment before after)
                (forward-line 1)
                (when (looking-at "^Comment: \\(.*\\)$")
                  (setq comment (match-string-no-properties 1)))
                (when (re-search-forward "^.* BEFORE\n" end t)
                  (let ((b-start (point)))
                    (when (re-search-forward "==== AFTER\n" end t)
                      (setq before (buffer-substring-no-properties
                                    b-start (match-beginning 0)))
                      (let ((a-start (point)))
                        (when (re-search-forward "^>>>>\\(\n\\|$\\)" end t)
                          (setq after (buffer-substring-no-properties
                                       a-start (match-beginning 0)))
                          (list file comment before after start end)
                          )))))))))))
    ))

;; Finds all occurrences of TEXT in the current buffer.
;; First tries an exact search. If no matches are found, falls back
;; to a whitespace-insensitive regular expression search.
;; Returns a list of (START . END) cons cells.
(defun llm-edit--find-matches (text)
  "Find all occurrences of TEXT in the current buffer.
Returns a list of (START . END) positions. Falls back to a
whitespace-insensitive search if no exact matches are found."
  (save-excursion
    (goto-char (point-min))
    (let ((matches nil))
      (while (search-forward text nil t)
        (push (cons (match-beginning 0) (match-end 0)) matches))
      (when (null matches)
        (goto-char (point-min))
        (let ((ws-rx (replace-regexp-in-string
                      "[ \t\n\r]+" "[ \t\n\r]+"
                      (regexp-quote text) t t)))
          (while (re-search-forward ws-rx nil t)
            (push (cons (match-beginning 0) (match-end 0)) matches))))
      (nreverse matches))))

;; This is used to narrow the before/after text to just the lines
;; that have changed. It is common for there to be context in the
;; before text to make the match unambiguous, and for those same
;; lines to be unchanged in the after text.
(defun llm-edit--trim-common-lines (before after)
  "Trim common lines from the beginning and end of BEFORE and AFTER.
Returns a list (BEFORE-TRIMMED AFTER-TRIMMED PREFIX-LEN SUFFIX-LEN)."
  (let* ((get-lines (lambda (str)
                      (let ((start 0) (lines nil))
                        (while (string-match "\n" str start)
                          (push (substring str start (match-end 0)) lines)
                          (setq start (match-end 0)))
                        (when (< start (length str))
                          (push (substring str start) lines))
                        (nreverse lines))))
         (b-lines (funcall get-lines before))
         (a-lines (funcall get-lines after))
         (prefix-len 0)
         (suffix-len 0))
    (while (and b-lines a-lines (equal (car b-lines) (car a-lines)))
      (setq prefix-len (+ prefix-len (length (car b-lines))))
      (setq b-lines (cdr b-lines))
      (setq a-lines (cdr a-lines)))
    (let ((b-tail (reverse b-lines))
          (a-tail (reverse a-lines)))
      (while (and b-tail a-tail (equal (car b-tail) (car a-tail)))
        (setq suffix-len (+ suffix-len (length (car b-tail))))
        (setq b-tail (cdr b-tail))
        (setq a-tail (cdr a-tail)))
      (list (apply #'concat (reverse b-tail))
            (apply #'concat (reverse a-tail))
            prefix-len
            suffix-len))))

;; Computes the colorized diff text to be inserted into the preview.
;; Extracted to allow for a fancier comparison algorithm in the future.
(defun llm-edit--colorize-diff (before after)
  "Return colorized text to insert, replacing the before text."
  (concat
   (when (> (length before) 0)
     (propertize before
                 'font-lock-face 'diff-removed
                 'face 'diff-removed))
   (when (> (length after) 0)
     (propertize after
                 'font-lock-face 'diff-added
                 'face 'diff-added))))

;; Creates a temporary clone of the target buffer to safely preview
;; the proposed changes. We apply the colored diff to the clone and
;; display it in a window, recentering if necessary to ensure the
;; changed lines are visible to the user.
(defun llm-edit--display-match-preview (target-buf match before after)
  "Create a clone of TARGET-BUF, apply the diff, and display it.
MATCH is a cons (START . END) of the matched region for BEFORE.
Returns (CLONE-BUF TARGET-BUF)."
  (let* ((match-start (car match))
         (match-end (cdr match))
         (exact-p (with-current-buffer target-buf
                    (string= (buffer-substring-no-properties
                              match-start match-end)
                             before)))
         (clone-buf (generate-new-buffer
                     (format " *edit-clone: %s*" (buffer-name target-buf))))
         (win (or (get-buffer-window target-buf)
                  (window-in-direction 'above)
                  (display-buffer target-buf))))
    (with-current-buffer clone-buf
      (insert-buffer-substring target-buf)
      (let ((major (buffer-local-value 'major-mode target-buf)))
        (when (fboundp major)
          (ignore-errors (funcall major)))))
    (with-current-buffer clone-buf
      (cl-destructuring-bind (t-before t-after p-len s-len)
          (if exact-p
              (llm-edit--trim-common-lines before after)
            (list before after 0 0))
        (let* ((m-start (+ match-start p-len))
               (m-end (- match-end s-len)))
          (ignore-errors (font-lock-ensure))
          (delete-region m-start m-end)
          (goto-char m-start)
          (insert (llm-edit--colorize-diff t-before t-after))
          (let ((diff-end (point)))
            (setq buffer-read-only t)
            (when win
              (set-window-buffer win clone-buf)
              (set-window-point win m-start)
              (with-selected-window win
                (let* ((diff-lines (count-lines m-start diff-end))
                       (win-lines (window-body-height))
                       (fit-p (<= (+ diff-lines 4) win-lines)))
                  (recenter (if fit-p
                                (/ (- win-lines diff-lines) 2)
                              2)))))))))
    (list clone-buf target-buf)))


;; Automatically shows a live preview of the edit block under point in its
;; target buffer.
;; Intended to be bound to post-command-hook.
;;
;; Caches the current block's state to avoid thrashing and rebuilding
;; the preview on every single cursor movement. Verifies the "before" text
;; exists exactly once in the target file, warning the user if the match
;; is missing or ambiguous. Visually renders the proposed change by creating
;; a clone of the target buffer and applying the diff to the clone, avoiding
;; any actual mutation of the target buffer.
(defun llm-edit--preview ()
  "Preview the edit block under point."
  (condition-case err
      (let ((block (llm-edit--parse-current-block))
            (edits-buf (current-buffer)))
        (if (not block)
            (progn
              (llm-edit--cleanup-preview)
              (setq llm-edit--last-block-state nil))
          (cl-destructuring-bind (file comment before after start end) block
            (let ((state (list start before after)))
              (unless (equal state llm-edit--last-block-state)
                (setq llm-edit--last-block-state state)
                (llm-edit--cleanup-preview)
                (when (and file before)
                  (let ((target-buf (find-file-noselect file)))
                    (with-current-buffer target-buf
                      (save-excursion
                        (let* ((matches (llm-edit--find-matches before))
                               (count (length matches)))
                          (if (/= count 1)
                              (with-current-buffer edits-buf
                                (message (if (= count 0) "Match Not Found"
                                           "Ambiguous Match")))
                            (let ((clone-info (llm-edit--display-match-preview
                                               target-buf (car matches) before after)))
                              (with-current-buffer edits-buf
                                (setq llm-edit--active-clone-info clone-info))))))))))))))
    (error
     (message "llm-edit preview error: %S" err))))

;; Finishes the currently focused edit block, optionally applying it to
;; the target buffer.
;;
;; If applied, re-verifies that the exact "before" text exists strictly once
;; in the target buffer, failing safely if the file has changed in a way that
;; makes the patch ambiguous. Cleans up the preview and removes the
;; block from the *EDITS* buffer.
(defun llm-edit--finish-block (apply-p)
  "Finish the edit block at point, applying it if APPLY-P is non-nil."
  (let ((block (llm-edit--parse-current-block)))
    (unless block
      (user-error "Not inside an edit block"))
    (cl-destructuring-bind (file comment before after start end) block
      (when apply-p
        (let ((target-buf (find-file-noselect file)))
          (with-current-buffer target-buf
            (save-excursion
              (let* ((matches (llm-edit--find-matches before))
                     (count (length matches)))
                (if (/= count 1)
                    (user-error
                     "Cannot apply: text not found exactly once (%d)" count)
                  (let* ((match (car matches))
                         (m-start (car match))
                         (m-end (cdr match)))
                    (delete-region m-start m-end)
                    (goto-char m-start)
                    (insert after))))))))
      (llm-edit--cleanup-preview)
      (setq llm-edit--last-block-state nil)
      (let ((inhibit-read-only t))
        (delete-region start end)
        (when (eq (char-after) ?\n)
          (delete-char 1))))))

(defun llm-edit-apply ()
  "Apply the edit block under point and remove it."
  (interactive)
  (llm-edit--finish-block t))

(defun llm-edit-reject ()
  "Reject the edit block under point and remove it."
  (interactive)
  (llm-edit--finish-block nil))

(defun llm-edit-quit ()
  "Kill the *EDITS* buffer and maximize the previous window.
Also terminates the background edit process if it is still running."
  (interactive)
  (let ((proc (get-buffer-process (current-buffer))))
    (when proc (delete-process proc)))
  (quit-window t)
  (delete-other-windows))

(defun llm-edit-target-move-up (&optional n)
  "Scroll the target buffer to show earlier lines (like moving cursor up)."
  (interactive "p")
  (let* ((block (llm-edit--parse-current-block))
         (file (and block (car block)))
         (target-buf (and file (find-file-noselect file)))
         (clone-buf (and llm-edit--active-clone-info
                         (car llm-edit--active-clone-info)))
         (win (or (and clone-buf (get-buffer-window clone-buf))
                  (and target-buf (get-buffer-window target-buf)))))
    (if win
        (with-selected-window win
          (scroll-down-line n))
      (user-error "Target buffer is not visible"))))

(defun llm-edit-target-move-down (&optional n)
  "Scroll the target buffer to show later lines (like moving cursor down)."
  (interactive "p")
  (let* ((block (llm-edit--parse-current-block))
         (file (and block (car block)))
         (target-buf (and file (find-file-noselect file)))
         (clone-buf (and llm-edit--active-clone-info
                         (car llm-edit--active-clone-info)))
         (win (or (and clone-buf (get-buffer-window clone-buf))
                  (and target-buf (get-buffer-window target-buf)))))
    (if win
        (with-selected-window win
          (scroll-up-line n))
      (user-error "Target buffer is not visible"))))

(defvar llm-edits-mode-map
  (let ((map (make-sparse-keymap)))
    (define-key map (kbd "C-c C-c") #'llm-edit-apply)
    (define-key map (kbd "C-c C-r") #'llm-edit-reject)
    (define-key map (kbd "C-c C-k") #'llm-edit-quit)
    (define-key map (kbd "M-<down>") #'llm-edit-target-move-down)
    (define-key map (kbd "M-<up>") #'llm-edit-target-move-up)
    map)
  "Keymap for `llm-edits-mode'.")

(define-derived-mode llm-edits-mode text-mode "LLM Edits"
  "Major mode for reviewing LLM edits."
  (setq-local font-lock-defaults nil)
  (setq-local header-line-format
              '(:eval (concat
                       (propertize (format "edits: %d | "
                                           (llm-edit--count-remaining))
                                   'face 'font-lock-keyword-face)
                       (propertize "C-c + Apply: C-c   "
                                   'face 'font-lock-comment-face)
                       (propertize "Reject: C-r   "
                                   'face 'font-lock-comment-face)
                       (propertize "Quit: C-k   "
                                   'face 'font-lock-comment-face)
                       (propertize "Scroll: Alt-up/dn"
                                   'face 'font-lock-comment-face))))
  (add-hook 'post-command-hook #'llm-edit--preview nil t)
  (add-hook 'kill-buffer-hook #'llm-edit--cleanup-preview nil t))

;; The main interactive entry point. Gathers the task prompt from the active
;; region or the minibuffer, and spawns the asynchronous external process.
;;
;; Saves all buffers beforehand to ensure the external process reads the
;; latest state from disk. Sets up a process pipeline that captures text
;; within replacement tags and feeds it to the JSON processor.
(defun llm-edit (prompt)
  "Invoke edit.exe to propose multi-file code replacements."
  (interactive
   (list (if (use-region-p)
             (let ((text (buffer-substring-no-properties
                          (region-beginning) (region-end))))
               (delete-region (region-beginning) (region-end))
               text)
           (read-string "LLM Task: "))))
  (save-some-buffers t)
  (let* ((filename (buffer-file-name))
         (dir default-directory)
         (edit-command
          (append (list edit-exe filename)
                  (apply #'append
                         (mapcar (lambda (cfg) (list "-config" cfg))
                                 edit-configs))
                  ))
         )
    (unless filename
      (error "Current buffer is not visiting a file"))

    (eprocs-run
     :name "llm-edit"
     :buffer "*EDITS*"
     :command edit-command
     :input prompt
     :pipeline (list
                (eprocs-make-tag-filter
                 ;; Avoid having the replacement marker literally
                 ;; in the mode's source file.
                 (concat "<" "REPLACEMENT>")
                 (concat "</" "REPLACEMENT>")
                 #'llm-edit--process-json)
                #'eprocs-filter-ansi-colors))
    (with-current-buffer (get-buffer-create "*EDITS*")
      (setq default-directory dir)
      (llm-edits-mode)
      (display-buffer (current-buffer)
                      '(display-buffer-at-bottom (window-height . 0.35))))))

(provide 'edit)
