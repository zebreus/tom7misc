
(defun gemini-replace-region (start end)
  "Send the current region to the Gemini CLI and replace it with the output."
  ; r passes the current region as start/end
  (interactive "r")

  (let ((coding-system-for-read 'utf-8-unix)
        (coding-system-for-write 'utf-8-unix)
        (command "c:/code/sf_svn/httpc/aget.exe"))

    (shell-command-on-region start end command
                             ;; insert into current buffer
                             t
                             ;; replace original region
                             t
                             ;; stderr
                             "*Gemini Error*")))

(global-set-key (kbd "C-c g") 'gemini-replace-region)
