;;; eprocs.el --- Process utilities -*- lexical-binding: t -*-
;;; Lexical binding is required!

;; custom process-based protocol for emacs helper programs that
;; I wrote. the "eproc" stuff should become a library of utilities,
;; but this is also mixed with a specific application (fill-in).

(require 'ansi-color)
(require 'cl-lib)

(defun eprocs-filter-ansi-colors (start end)
  "Parses and applies ANSI color codes in the region."
  (ansi-color-apply-on-region start end))

;; Could actually handle ansi prevline etc. here, but
;; it can get messy! Maybe we could use a single marker for
;; the "ansi cursor"?

;; we look for magic tokens that emacs itself processes.
;; This constant needs to exceed the longest of those,
;; so that we can handle the case that it crosses an
;; update boundary.
;; Note: Includes ansi sequences that we process by
;; ansi-color-apply-on-region!
(defvar eprocs-max-magic-size 80)


(defun eprocs-internal-tag-filter (open-tag close-tag callback
                                            start-marker end-marker)
  "Creates a filter that acts on a <OPEN>...</CLOSE> sequence.
   Use eprocs-make-tag-filter to call this."
  (save-excursion
    (goto-char start-marker)
    (while (search-forward close-tag end-marker t)
      (let ((close-end (point))
            (close-start (match-beginning 0)))
        (if (search-backward open-tag nil t)
            (let* ((open-start (point))
                   (open-end (match-end 0))
                   (payload (buffer-substring-no-properties
                             open-end close-start)))
              (delete-region open-start close-end)
              (funcall callback payload)
              (goto-char open-start))
          (goto-char close-end))))))

;; creates a pipeline stage that finds matched tags. call like:
;;    eprocs-make-tag-filter "<OPEN>" "</CLOSE>" #'callback-fn
;; where callback-fn just takes the captured string. the
;; tags and contents are removed from the buffer.
(defun eprocs-make-tag-filter (open-tag close-tag f)
  "Makes a start/end tag filter that calls f."
  (apply-partially #'eprocs-internal-tag-filter open-tag close-tag f))

(cl-defun eprocs-run (&key name buffer command input pipeline
                           on-success on-error)
  "Run an async process with a predefined text filtering pipeline.
    NAME is the process name.
    BUFFER is the buffer name.
    COMMAND is a list of strings (the executable and args).
    INPUT is a string sent to stdin (optional).
    PIPELINE is a list of filter functions.
    ON-SUCCESS and ON-ERROR are optional callbacks."
  (let* ((buf-name (or buffer (format "*%s*" name)))
         (out-buf (get-buffer-create buf-name)))

    ;; Output buffer, at the bottom.
    (with-current-buffer out-buf
      (let ((inhibit-read-only t))
        (erase-buffer)
        (compilation-minor-mode 1)))
    (display-buffer out-buf '(display-buffer-at-bottom
                              (window-height . 0.25)))

    ;; Start the process
    (let ((proc
           (make-process
            :name name
            :buffer out-buf
            :command command
            :connection-type 'pipe

            :filter
            (lambda (process output)
              (let ((buf (process-buffer process)))
                (when (buffer-live-p buf)
                  (with-current-buffer buf
                    (let* ((inhibit-read-only t)
                           (end-marker (process-mark process))
                           (moving-buffer (= (point) end-marker))
                           (moving-windows nil))

                      ;; Check which windows are auto-scrolling
                      (dolist (win (get-buffer-window-list buf nil t))
                        (when (= (window-point win) end-marker)
                          (push win moving-windows)))

                      (save-excursion
                        (goto-char end-marker)
                        (let ((start-marker
                               (copy-marker
                                (max (point-min)
                                     (- (point) eprocs-max-magic-size)))))
                          (insert output)
                          (set-marker end-marker (point))
                          
                          ;; Run the configured pipeline stages
                          (dolist (filter-func pipeline)
                            (funcall filter-func start-marker end-marker))
                          
                          (set-marker start-marker nil)))

                      ;; Auto-scroll the windows
                      (dolist (win moving-windows)
                        (set-window-point win end-marker))
                      (when moving-buffer 
                        (goto-char end-marker)))))))

            :sentinel
            (lambda (_process event)
             (cond
               ((string= event "finished\n")
                (when on-success (funcall on-success)))
               ((string-prefix-p "exited abnormally" event)
                (when on-error (funcall on-error (string-trim event))))
               ;; e.g. signals
               (t
                (when on-error
                  (funcall on-error
                           (format "Fatal: %s" (string-trim event))))))))))

      ;; Send input and EOF
      (when input
        (process-send-string proc input))
      (process-send-eof proc)
      
      ;; Return the process object just in case the caller wants it
      proc)))

(provide 'eprocs)
