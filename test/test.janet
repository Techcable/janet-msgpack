(import msgpack)


(def data-dir "data")
(defn run-test [data-file]
  (def path (string/join [data-dir data-file] "/"))
  (print "Testing: " path)
  (def f (file/open path))
  (def data (file/read f :all))
  (file/close f)
  (def actual-data (msgpack/decode data))
  (def proc (def {:out expected-data-out} (os/spawn
    ["python3" "reference.py" path]
    :exp
    {:out :pipe})))
  (def expected-data-text (:read expected-data-out :all))
  (os/proc-wait proc)
  (def expected-data (eval-string expected-data-text))
  (if (deep-not= expected-data actual-data)
    (do
      (print "Expected data:")
      (print (pp expected-data))
      (print)
      (print "Actual data:")
      (print (pp actual-data))
    )))
(each test-file (os/dir data-dir) (run-test test-file))


