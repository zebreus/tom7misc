;;;       -*- lexical-binding: t -*-
;;; Lexical binding is required!

;; fill-in example.
;;
;; runs a subprocess with the contents of the region as its stdin.
;; When the subprocess contains <REPLACEMENT>str</REPLACEMENT>, it
;; replaces the region with that string.
;;
;; Since the process runs asynchronously, we probably want to
;; replace the region with some kind of explicit marker, like
;; ** XXX FILL-IN RESULT HERE **
;; so that we can search-and-replace it when the callback is run.

(require 'eprocs)

(defgroup fill-in nil
  "Settings for fill-in command integration."
  :group 'external)

;; Faces for placeholder text.
(defface fill-in-processing-face
  '((t :foreground "cyan" :background "#003366"))
  "Face for the active fill-in processing placeholder."
  :group 'fill-in)

(defface fill-in-error-face
  '((t :inherit error :background "#003366"))
  "Face for the fill-in failure placeholder."
  :group 'fill-in)

(defcustom fill-in-exe "c:\\code\\sf_svn\\httpc\\fill-in.exe"
  "The executable to use for the fill-in command."
  :type 'file
  :group 'fill-in)

(defcustom fill-in-default-directories
  '("/d/tom/llm"
    ; "/c/code/sf_svn/cc-lib"
    )
  "List of directories that are suggested by default for fill-in."
  :type '(repeat directory)
  :group 'fill-in)

(defcustom fill-in-configs
  '("c:\\code\\sf_svn\\sitelisp\\model-config.txt"
    ; ...
    )
  "List of config files passed to fill-in."
  :type '(repeat file)
  :group 'fill-in)

(defvar-local fill-in--font-lock-set-up-p nil)

(defun fill-in--set-up-font-lock ()
  "Set up font-lock rules for placeholders in the current buffer,
   overriding comments."
  (unless fill-in--font-lock-set-up-p
    (font-lock-add-keywords
     nil
     ;; final t makes this override comment face
     '(("\\[\\[ \\.\\.\\. FILL-IN PROCESSING [0-9a-f]+ \\.\\.\\. \\]\\]"
        0 'fill-in-processing-face t)
       ("\\[\\[ FILL-IN FAILED.*?\\]\\]" 0 'fill-in-error-face t))
     t)
    (setq fill-in--font-lock-set-up-p t)))

(defun do-fill-in (replacement target-buffer placeholder)
  "Find the exact placeholder in target-buffer and replace it with the
   replacement."
  (if (not (buffer-live-p target-buffer))
      (message "Fill-in canceled: Target buffer was killed.")
    (with-current-buffer target-buffer
      (save-excursion
        (goto-char (point-min))
        (if (search-forward placeholder nil t)
            (progn
              ;; plain text search/replace.
              (replace-match replacement t t)
              (message "fill-in successful."))
          (message "Placeholder not found?"))))))

(defun fill-in (beg end)
  "Saves buffer, runs a command with region as stdin, and processes output."
  (interactive "r")

  ;; Remove trailing newlines from the region, since the replacement
  ;; text does not have a newline.
  (save-excursion
    (goto-char end)
    (skip-chars-backward "\n\r" beg)
    (setq end (point)))

  (unless buffer-file-name
    (error "Need to save the file so that we can pass it to the program."))

  ;; Set up custom font-lock rules for this buffer
  (fill-in--set-up-font-lock)

  (let* ((region-text (buffer-substring-no-properties beg end))
         (filename (buffer-file-name))
         (target-buffer (current-buffer))
         (placeholder-for-cmd "** FILL IN THE ANSWER HERE **")
         
         ;; Generate a short random nonce.
         (nonce (format "%06x" (random #xffffff)))

         (fill-in-command
          (append (list fill-in-exe filename)
                  (apply #'append
                         (mapcar (lambda (dir) (list "-dir" dir))
                                 fill-in-default-directories))
                  (apply #'append
                         (mapcar (lambda (cfg) (list "-config" cfg))
                                 fill-in-configs))
                  ))
         
         ;; Direct check for C-family languages
         (is-c-like (derived-mode-p 'c-mode 'c++-mode 'objc-mode
                                    'java-mode 'js-mode 'typescript-mode))
         (c-start (if is-c-like "/*"
                    (if comment-start (string-trim-right comment-start) "")))
         (c-end   (if is-c-like "*/"
                    (if comment-end (string-trim-left comment-end) "")))
         
         ;; Construct the exact placeholder string with the nonce.
         (placeholder
          (if (string= c-end "")
              (format "\n%s [[ ... FILL-IN PROCESSING %s ... ]]\n"
                      c-start nonce)
            (format "%s [[ ... FILL-IN PROCESSING %s ... ]] %s"
                    c-start nonce c-end))))

    ;; We save a version of the file with placeholder-for-cmd (so that
    ;; the subprocess can see that), and then replace that with the
    ;; placeholder that marks the destination for the result. Try to
    ;; do this in a way that is robust even if (save-buffer) moves
    ;; the cursor around or removes tabs, etc.
    (save-excursion
      (let ((insert-start (copy-marker beg))
            (insert-end (make-marker)))

        ;; Put placeholder-for-cmd.
        (delete-region beg end)
        (goto-char insert-start)
        (insert placeholder-for-cmd)
        (set-marker insert-end (point))

        (save-buffer)

        ;; Swap the fixed text for the async placeholder
        (delete-region insert-start insert-end)
        (goto-char insert-start)
        (insert placeholder)
        
        (set-marker insert-start nil)
        (set-marker insert-end nil)))
    
    (eprocs-run
     :name (format "fill-in-%s" nonce)
     :buffer (format "*Fill In Async %s*" nonce)
     :command fill-in-command
     :input region-text
     :pipeline
     (list #'eprocs-filter-ansi-colors
           ;; find the text in this pair of tags and pass it to
           ;; do-fill-in.
           (eprocs-make-tag-filter 
            "<REPLACEMENT>" "</REPLACEMENT>"
            (lambda (payload)
              (do-fill-in payload target-buffer placeholder))))
     
     ;; Fallback if the replacement text was never found.
     :on-success
     (lambda ()
       (when (buffer-live-p target-buffer)
         (with-current-buffer target-buffer
           (save-excursion
             (goto-char (point-min))
             ;; If the placeholder is still there, do-fill-in didn't run.
             (when (search-forward placeholder nil t)
               (let ((err-msg (format
                               "%s [[ FILL-IN FAILED ]] %s"
                               c-start c-end)))
                 (replace-match err-msg t t)))))))
     
     ;; Signal, etc.
     :on-error
     (lambda (err)
       (when (buffer-live-p target-buffer)
         (with-current-buffer target-buffer
           (save-excursion
             (goto-char (point-min))
             ;; If the process crashed, put the user's original text back
             (when (search-forward placeholder nil t)
               (replace-match region-text t t)))))
       (message "Error: Process failed! OS reported: %s" err)))))

(provide 'fill-in)
