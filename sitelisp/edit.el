;;;       -*- lexical-binding: t -*-
;;; Lexical binding is required!

;; Runs an external utility "edit.exe" get LLM-driven edits,
;; and interactively applies them.

; Pro-tip: Use M-x toggle-debug-on-error

;; TODO:
;;  - Show key commands in *EDITS* buffer
;;  - Show status (how many edits left) in edits buffer
;;  - Better diff display!
;;  - colorization for *EDITS* buffer

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

(defvar-local llm-edit--active-overlay nil)
(defvar-local llm-edit--last-block-state nil)

;; Processes a JSON payload received from the external edit process.
;; Extracts the file, comment, and before/after text blocks, formatting
;; them into a structured representation in the *EDITS* buffer.
;;
;; Normalizes CRLF to LF to prevent text-matching issues on Windows.
;; Crucially, applies read-only and rear-nonsticky text properties to the
;; structural delimiters (like "<<<< BEFORE"). This prevents the user from
;; accidentally corrupting the boundaries while tweaking the proposed edits,
;; ensuring the parser won't break later.
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
                (let ((inhibit-read-only t))
                  (insert (propertize (format "File: %s\n" file)
                                      'read-only t 'rear-nonsticky t))
                  (insert (propertize (format "Comment: %s\n" comment)
                                      'read-only t 'rear-nonsticky t))
                  (insert (propertize "<<<< BEFORE\n"
                                      'read-only t 'rear-nonsticky t))
                  (insert before)
                  (insert (propertize "==== AFTER\n"
                                      'read-only t 'rear-nonsticky t))
                  (insert after)
                  (insert (propertize ">>>>\n\n"
                                      'read-only t 'rear-nonsticky t))))))))
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

;; Automatically shows a live preview of the edit block under point in its
;; target buffer. Intended to be bound to post-command-hook.
;;
;; Caches the current block's state to avoid thrashing and rebuilding
;; overlays on every single cursor movement. Verifies the "before" text
;; exists exactly once in the target file, warning the user if the match
;; is missing or ambiguous. Visually renders the proposed change inline
;; using an overlay with an after-string, avoiding any actual mutation
;; of the target buffer.
(defun llm-edit--preview ()
  "Preview the edit block under point."
  (let ((block (llm-edit--parse-current-block)))
    (if (not block)
        (progn
          (when llm-edit--active-overlay
            (delete-overlay llm-edit--active-overlay)
            (setq llm-edit--active-overlay nil))
          (setq llm-edit--last-block-state nil))
      (cl-destructuring-bind (file comment before after start end) block
        (let ((state (cons start after)))
          (unless (equal state llm-edit--last-block-state)
            (setq llm-edit--last-block-state state)
            (when llm-edit--active-overlay
              (delete-overlay llm-edit--active-overlay)
              (setq llm-edit--active-overlay nil))
            (when (and file before)
              (let ((target-buf (find-file-noselect file)))
                (with-current-buffer target-buf
                  (save-excursion
                    (let* ((matches (llm-edit--find-matches before))
                           (count (length matches)))
                      (if (/= count 1)
                          (with-current-buffer (get-buffer "*EDITS*")
                            (message (if (= count 0) "Match Not Found"
                                       "Ambiguous Match")))
                        (let* ((match (car matches))
                               (match-start (car match))
                               (match-end (cdr match)))
                          (let ((win (window-in-direction 'above)))
                            (when win
                              (set-window-buffer win target-buf)
                              (set-window-point win match-start)))
                          (let ((ov (make-overlay match-start match-end)))
                            (overlay-put ov 'face 'diff-removed)
                            (let ((after-str
                                   (propertize after 'face 'diff-added)))
                              (overlay-put ov 'after-string after-str))
                            (with-current-buffer (get-buffer "*EDITS*")
                              (setq llm-edit--active-overlay ov))))))))))))))))

;; Finishes the currently focused edit block, optionally applying it to
;; the target buffer.
;;
;; If applied, re-verifies that the exact "before" text exists strictly once
;; in the target buffer, failing safely if the file has changed in a way that
;; makes the patch ambiguous. Cleans up the preview overlay and removes the
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
      (when llm-edit--active-overlay
        (delete-overlay llm-edit--active-overlay)
        (setq llm-edit--active-overlay nil))
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

(defun llm-edit-target-move-up (&optional n)
  "Scroll the target buffer to show earlier lines (like moving cursor up)."
  (interactive "p")
  (let* ((block (llm-edit--parse-current-block))
         (file (and block (car block)))
         (buf (and file (find-file-noselect file)))
         (win (and buf (get-buffer-window buf))))
    (if win
        (with-selected-window win
          (scroll-down-line n))
      (user-error "Target buffer is not visible"))))

(defun llm-edit-target-move-down (&optional n)
  "Scroll the target buffer to show later lines (like moving cursor down)."
  (interactive "p")
  (let* ((block (llm-edit--parse-current-block))
         (file (and block (car block)))
         (buf (and file (find-file-noselect file)))
         (win (and buf (get-buffer-window buf))))
    (if win
        (with-selected-window win
          (scroll-up-line n))
      (user-error "Target buffer is not visible"))))

(defvar llm-edits-mode-map
  (let ((map (make-sparse-keymap)))
    (define-key map (kbd "C-c C-c") #'llm-edit-apply)
    (define-key map (kbd "C-c C-r") #'llm-edit-reject)
    (define-key map (kbd "M-<down>") #'llm-edit-target-move-down)
    (define-key map (kbd "M-<up>") #'llm-edit-target-move-up)
    map)
  "Keymap for `llm-edits-mode'.")

(define-derived-mode llm-edits-mode text-mode "LLM Edits"
  "Major mode for reviewing LLM edits."
  (setq-local font-lock-defaults nil)
  (add-hook 'post-command-hook #'llm-edit--preview nil t)
  (add-hook 'kill-buffer-hook
            (lambda ()
              (when llm-edit--active-overlay
                (delete-overlay llm-edit--active-overlay)))
            nil t))

;; The main interactive entry point. Gathers the task prompt from the active
;; region or the minibuffer, and spawns the asynchronous external process.
;;
;; Saves all buffers beforehand to ensure the external process reads the
;; latest state from disk. Sets up a process pipeline that captures text
;; within replacement tags and feeds it to the JSON processor. The tags are
;; constructed dynamically (concatenating "<" and "REPLACEMENT>") to prevent
;; the filter from accidentally matching its own source code when this specific
;; file is being edited.
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
      (llm-edits-mode)
      (display-buffer (current-buffer)
                      '(display-buffer-at-bottom (window-height . 0.35))))))

(provide 'edit)
