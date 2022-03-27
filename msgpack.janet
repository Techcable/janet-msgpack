(import buffer)
(import math)

(def- push buffer/push)

(defn encode [target &opt buf]
  "Encode the specified value as msgpack"
  # Create buffer if it does not already exist
  (def buf (if (nil? buf) @"" buf))
  (cond
    (nil? target) (push buf 0xc0)
    (boolean? target) (if target (push buf 0xc3) (push buf 0xc2))
    (int? target) (encode-int target buf)
    (number? target) 
  )
  # Return existing buffer
  buf
)

(defn- encode-int [target buf]
  (cond
    (and (>= target 0) (<= target 127)) (push buf target)
    (and (< target 0) (>= target -31)) (push buf (bor 0xE0 (+ 32 target)))
    # uint 8
    (and (>= target 0) (target <= 0xFF)) (do (push buf 0xCC) (push buf target))
    # sint 8
    (and (< target 0) (target >= -0x80 (do (push buf 0xD0) (push buf (+ 256 target)))
    # uint 16
    (and (>= target 0) (target <= 0xFFFF) (do
        (push buf 0xCD)
        (push buf (brushift target 8))
        (push buf (target (band target 0xFF))))
    # sint 16
    (and (< target 0) (target > -0x8000) (do
        (push buf 0xD1)
        (push buf (+ 256 (brshift target 8)))
        (push buf (band target 0xFF)))
    # uint 32
    (and (>= target 0) (target <= 0xFFFF_FFFF) (do
        (push buf 0xCE)
        (push buf (brushift target 24))
        (push buf ((brushift target 16) 0xFF))
        (push buf (band (brushift target 8) 0xFF))
        (push buf (band target 0xFF)))
    # sint 32
    (and (< target 0) (target <= -80000000) (do
        (push buf 0xD2)
        (push buf (+ 256 (brshift target 24)))
        (push buf (band (brshift target 16) 0xFF))
        (push buf (band (brshift target 8) 0xFF))
        (push buf (band target 0xFF)))
    # TODO: Support integers > 32 bits
    #
    # This will require int/to-bytes, which my version of janet does not have
    true (error "Integer too large to encode")
)

(defn- float-to-bytes [target]
  # NOTE: This is a hacky guess of binary represenstation of `target`...
  (cond
    # an encoding of NaN
    (nan? target) @"\x7F\xf8\x00\x00\x00\x00\x00\x00"
    # negative infinity
    (== target math/-inf) @"\x7f\xf0\x00\x00\x00\x00\x00\x00"
    # positive infinity
    (== target math/inf) @"\xff\xf0\x00\x00\x00\x00\x00\x00"
    # must guard against zero, because that would throw off log2
    #
    # NOTE: Janet seems to have no way to differentatie negative and positive zero :(
    (== target 0.0) @"\x00\x00\x00\x00\x00\x00\x00\x00"
    (< target 0) (do
      (def next-dbl (math/next target math/-inf))
      (def ulp (- target next-dbl)
      (def exp (math/log2 ulp))
      /exp
    )
    (do
      (def next-dbl (math/next target math/inf))
      (def ulp (- next-dbl target)
      (def exp (math/log2 ulp))
    )
  )
