open Thrift

module P = Protocol

let get_byte i b = 255 land (i lsr (8*b))
let get_byte64 i b = 255 land (Int64.to_int (Int64.shift_right i (8*b)))


let tv = P.t_type_to_i
let vt = P.t_type_of_i


let comp_int b n = 
  let s = ref 0l in
  let sb = 32 - 8*n in
    for i=0 to (n-1) do
      s:= Int32.logor !s (Int32.shift_left (Int32.of_int (int_of_char b.[i])) (8*(n-1-i)))
    done;
    Int32.to_int (Int32.shift_right (Int32.shift_left !s sb) sb)

let comp_int64 b n =
  let s = ref 0L in
    for i=0 to (n-1) do
      s:=Int64.logor !s (Int64.shift_left (Int64.of_int (int_of_char b.[i])) (8*(n-1-i)))
    done;
    !s

let version_mask = 0xffff0000
let version_1 = 0x80010000

class t trans =
object (self)
  inherit P.t trans
  val ibyte = String.create 8
  method writeBool b = 
    ibyte.[0] <- char_of_int (if b then 1 else 0);
    trans#write ibyte 0 1
  method writeByte i =
    ibyte.[0] <- char_of_int (get_byte i 0);
    trans#write ibyte 0 1
  method writeI16 i =
    let gb = get_byte i in
      ibyte.[1] <- char_of_int (gb 0);
      ibyte.[0] <- char_of_int (gb 1);
      trans#write ibyte 0 2
  method writeI32 i =
    let gb = get_byte i in
      for i=0 to 3 do
        ibyte.[3-i] <- char_of_int (gb i)
      done;
      trans#write ibyte 0 4
  method writeI64 i=
    let gb = get_byte64 i in
      for i=0 to 7 do
        ibyte.[7-i] <- char_of_int (gb i)
      done;
      trans#write ibyte 0 8
  method writeDouble d =
    self#writeI64 (Int64.bits_of_float d)
  method writeString s=
    let n = String.length s in
      self#writeI32(n);
      trans#write s 0 n
  method writeBinary a = self#writeString a
  method writeMessageBegin (n,t,s) =
    self#writeI32 (version_1 lor (P.message_type_to_i t));
    self#writeString n;
    self#writeI32 s
  method writeMessageEnd = ()
  method writeStructBegin s = ()
  method writeStructEnd = ()
  method writeFieldBegin (n,t,i) =
    self#writeByte (tv t);
    self#writeI16 i
  method writeFieldEnd = ()
  method writeFieldStop =
    self#writeByte (tv (Protocol.T_STOP))
  method writeMapBegin (k,v,s) =
    self#writeByte (tv k);
    self#writeByte (tv v);
    self#writeI32 s
  method writeMapEnd = ()
  method writeListBegin (t,s) =
    self#writeByte (tv t);
    self#writeI32 s
  method writeListEnd = ()
  method writeSetBegin (t,s) =
    self#writeByte (tv t);
    self#writeI32 s
  method writeSetEnd = ()
  method readByte = 
    ignore (trans#readAll ibyte 0 1);
    (comp_int ibyte 1)
  method readI16 =
    ignore (trans#readAll ibyte 0 2);
    comp_int ibyte 2
  method readI32 =
    ignore (trans#readAll ibyte 0 4);
    comp_int ibyte 4
  method readI64 =
    ignore (trans#readAll ibyte 0 8);
    comp_int64 ibyte 8
  method readDouble =
    Int64.float_of_bits (self#readI64)
  method readBool =
    self#readByte = 1
  method readString =
    let sz = self#readI32 in
    let buf = String.create sz in
      ignore (trans#readAll buf 0 sz);
      buf
  method readBinary = self#readString
  method readMessageBegin =
    let ver = self#readI32 in
      if (ver land version_mask != version_1) then
        (print_int ver;
        raise (P.TProtocolExn (P.BAD_VERSION, "Missing version identifier")))
      else
        let s = self#readString in
        let mt = P.message_type_of_i (ver land 0xFF) in
          (s,mt, self#readI32)
  method readMessageEnd = ()
  method readStructBegin =
    ""
  method readStructEnd = ()
  method readFieldBegin =
    let t = (vt (self#readByte)) 
    in
      if t != P.T_STOP then
        ("",t,self#readI16)
      else ("",t,0);
  method readFieldEnd = ()
  method readMapBegin =
    let kt = vt (self#readByte) in
    let vt = vt (self#readByte) in
      (kt,vt, self#readI32)
  method readMapEnd = ()
  method readListBegin =
    let t = vt (self#readByte) in
    (t,self#readI32)
  method readListEnd = ()
  method readSetBegin =
    let t = vt (self#readByte) in
    (t, self#readI32);
  method readSetEnd = ()
end

class factory =
object
  inherit P.factory
  method getProtocol tr = new t tr
end
