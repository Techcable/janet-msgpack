(declare-project
  :name "msgpack"
  :description "An implementation of msgpack for Janet."
  :author "Techcable"
  :url "https://github.com/Techcable/janet-msgpack"
  :license "MIT"
  :version "0.1.0"
  :repo "https://github.com/Techcable/janet-msgpack")

(declare-native
  :name "msgpack"
  :cflags [(string "-I" "mpack/src/mpack") "-DMPACK_EXPECT=0" "-DMPACK_NODE=0" "-DMPACK_WRITER=0"]
  :source (flatten (tuple
    @["msgpack.c"]
    (map (fn [a] (string "mpack/src/mpack/mpack-" a ".c")) ["common" "platform" "reader"])
  )))
