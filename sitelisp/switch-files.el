;; swich-files.el: a method of switching between matched pairs of
;; files and for following include directives.
;;
;; Copyright (C) 2002-2004  Wes Hardaker <elisp@hardakers.net>
;;
;; Modified by Tom 7, 19 Dec 2009 and 22 Jul 2026.
;;
;; This program is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 2, or (at your option)
;; any later version.
;;
;; This program is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.
;;
;; A copy of the GNU General Public License can be obtained from this
;; program's author (send electronic mail to psmith@BayNetworks.com) or
;; from the Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA
;; 02139, USA.

;; here 'nil' is like "." but behaves better with stuff like "plink:" paths.
(defvar switch-files-paths '(nil "/usr/include" "/usr/local/include")
  "the list of paths to look through for matching files.")

;; There need to be multiple possible destinations for
;; certain suffixes, for example .h (.c, .cc, .cpp, etc.).
;; We open the first one that works.
(defvar switch-files-list '(
                (".h" ".cc" "_test.cc")
                (".h" ".c")
                (".H" ".C")
                (".h" ".cpp")
                ("-sig.sml" ".sml")
                )
  "A list of file rotation groups. The extensions are matched as suffixes.")

(require 'seq)

;; switch to the first file in the list for which we already have an open
;; buffer and return that filename. Returns nil if none have open buffers.
(defun switch-to-buffer-rec (fs)
  (if (null fs) nil
    (let* ((h (car fs))
       (tl (cdr fs)))
      (if (bufferp h)
      (progn
        (switch-to-buffer h)
        h)
    (switch-to-buffer-rec tl)))
    ))

;; Like above, but opens files with find-file.
;; looks in all the switch-files-paths.
(defun switch-by-opening-rec (fs)
  (if (null fs) nil
    (let* ((h (car fs))
       (tl (cdr fs))
       ;; modified in while loop
       (pathlist switch-files-paths))

      (setq success nil)
      (while (and (not success) pathlist)
    ;; (message "expand-file-name %s %s" h (car pathlist))
    (setq thefile (expand-file-name h (car pathlist)))
    ;; (message "file-exists-p %s" thefile)
    (if (file-exists-p thefile)
        (setq success t)
      (setq pathlist (cdr pathlist))))
      (if success
      (progn
        (find-file thefile)
        thefile)
    (switch-by-opening-rec tl)))))



(defun switch-files ()
  (interactive)
  (let* ((curbuffer (current-buffer))
     (buffername (buffer-file-name))
     ;; list of files to try switching to, in priority order
     (startfiles
      (apply
       #'append
       (mapcar
        (lambda (group)
          (let ((matches (seq-filter
                          (lambda (ext)
                            (and buffername
                                 (string-suffix-p ext buffername)))
                          group)))
            (if matches
                (let* ((match (car (seq-sort
                                    (lambda (a b) (> (length a) (length b)))
                                    matches)))
                       (base-len (- (length buffername) (length match)))
                       (basename (file-name-nondirectory
                                  (substring buffername 0 base-len)))
                       (pos (seq-position group match))
                       (rotated (append (nthcdr (1+ pos) group)
                                        (seq-take group pos))))
                  (mapcar (lambda (ext) (concat basename ext))
                          rotated))
              nil)))
        switch-files-list))))

    ;; body of let*
    ;; can we switch to anything in startfiles?

    (cond
     ;; first prefer open buffers
     ((switch-to-buffer-rec startfiles))
     ((switch-by-opening-rec startfiles))

     (t
      (message "(From buffer '%s' name '%s', no targets matched: %s"
           curbuffer buffername startfiles)))
    ))

(global-set-key "\C-x\M-f" 'switch-files)


(provide 'switch-files)
