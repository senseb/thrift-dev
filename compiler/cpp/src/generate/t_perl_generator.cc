// Copyright (c) 2006- Facebook
// Distributed under the Thrift Software License
//
// See accompanying file LICENSE or visit the Thrift site at:
// http://developers.facebook.com/thrift/

#include <stdlib.h>
#include <sys/stat.h>
#include <sstream>
#include "t_perl_generator.h"
using namespace std;

/**
 * Prepares for file generation by opening up the necessary file output
 * streams.
 *
 * @param tprogram The program to generate
 */
void t_perl_generator::init_generator() {
  // Make output directory
  mkdir(T_PERL_DIR, S_IREAD | S_IWRITE | S_IEXEC);

  string outdir(T_PERL_DIR);
  std::string ns = program_->get_perl_package();
  if (ns.length() > 0) {
    outdir += "/" + ns;
    mkdir(outdir.c_str(), S_IREAD | S_IWRITE | S_IEXEC);
  }

  // Make output file
  string f_types_name = outdir+"/Types.pm";
  f_types_.open(f_types_name.c_str());
  string f_consts_name = outdir+"/Constants.pm";
  f_consts_.open(f_consts_name.c_str());

  // Print header
  f_types_ <<
    autogen_comment() <<
    perl_includes();

  // Print header
  f_consts_ <<
    autogen_comment() <<
    endl;
}

/**
 * Prints standard java imports
 */
string t_perl_generator::perl_includes() {
  string inc;

  inc  = "require 5.6.0;\n";
  inc += "use strict;\n";
  inc += "use warnings;\n";
  inc += "use Thrift;\n\n";

  return inc;
}

/**
 * Close up (or down) some filez.
 */
void t_perl_generator::close_generator() {
  // Close types file
  f_types_ << "1;" << endl;
  f_types_.close();

  f_consts_ << "1;" << endl;
  f_consts_.close();
}

/**
 * Generates a typedef. This is not done in PERL, types are all implicit.
 *
 * @param ttypedef The type definition
 */
void t_perl_generator::generate_typedef(t_typedef* ttypedef) {}

/**
 * Generates code for an enumerated type. Since define is expensive to lookup
 * in PERL, we use a global array for this.
 *
 * @param tenum The enumeration
 */
void t_perl_generator::generate_enum(t_enum* tenum) {
  f_types_ << "package "<<tenum->get_name()<<";"<<endl;

  vector<t_enum_value*> constants = tenum->get_constants();
  vector<t_enum_value*>::iterator c_iter;
  int value = -1;
  for (c_iter = constants.begin(); c_iter != constants.end(); ++c_iter) {
    if ((*c_iter)->has_value()) {
      value = (*c_iter)->get_value();
    } else {
      ++value;
    }

    f_types_ << "use constant "<<(*c_iter)->get_name() << " => " << value << ";" << endl;
  }
}

/**
 * Generate a constant value
 */
void t_perl_generator::generate_const(t_const* tconst) {
  t_type* type = tconst->get_type();
  string name = tconst->get_name();
  t_const_value* value = tconst->get_value();

  f_consts_ << "use constant " << name << " => ";
  f_consts_ << render_const_value(type, value);
  f_consts_ << ";" << endl << endl;
}

/**
 * Prints the value of a constant with the given type. Note that type checking
 * is NOT performed in this function as it is always run beforehand using the
 * validate_types method in main.cc
 */
string t_perl_generator::render_const_value(t_type* type, t_const_value* value) {
  std::ostringstream out;

  while (type->is_typedef()) {
    type = ((t_typedef*)type)->get_type();
  }

  if (type->is_base_type()) {
    t_base_type::t_base tbase = ((t_base_type*)type)->get_base();
    switch (tbase) {
    case t_base_type::TYPE_STRING:
      out << "'" << value->get_string() << "'";
      break;
    case t_base_type::TYPE_BOOL:
      out << (value->get_integer() > 0 ? "1" : "0");
      break;
    case t_base_type::TYPE_BYTE:
    case t_base_type::TYPE_I16:
    case t_base_type::TYPE_I32:
    case t_base_type::TYPE_I64:
      out << value->get_integer();
      break;
    case t_base_type::TYPE_DOUBLE:
      if (value->get_type() == t_const_value::CV_INTEGER) {
        out << value->get_integer();
      } else {
        out << value->get_double();
      }
      break;
    default:
      throw "compiler error: no const of base type " + tbase;
    }
  } else if (type->is_enum()) {
    out << value->get_integer();
  } else if (type->is_struct() || type->is_xception()) {
    out << "new " << perl_namespace(type->get_program()) << type->get_name() << "({" << endl;
    indent_up();
    const vector<t_field*>& fields = ((t_struct*)type)->get_members();
    vector<t_field*>::const_iterator f_iter;
    const map<t_const_value*, t_const_value*>& val = value->get_map();
    map<t_const_value*, t_const_value*>::const_iterator v_iter;
    for (v_iter = val.begin(); v_iter != val.end(); ++v_iter) {
      t_type* field_type = NULL;
      for (f_iter = fields.begin(); f_iter != fields.end(); ++f_iter) {
        if ((*f_iter)->get_name() == v_iter->first->get_string()) {
          field_type = (*f_iter)->get_type();
        }
      }
      if (field_type == NULL) {
        throw "type error: " + type->get_name() + " has no field " + v_iter->first->get_string();
      }
      out << render_const_value(g_type_string, v_iter->first);
      out << " => ";
      out << render_const_value(field_type, v_iter->second);
      out << endl;
    }

    out << "})";
  } else if (type->is_map()) {
    t_type* ktype = ((t_map*)type)->get_key_type();
    t_type* vtype = ((t_map*)type)->get_val_type();
    out << "{" << endl;

    const map<t_const_value*, t_const_value*>& val = value->get_map();
    map<t_const_value*, t_const_value*>::const_iterator v_iter;
    for (v_iter = val.begin(); v_iter != val.end(); ++v_iter) {
      out << render_const_value(ktype, v_iter->first);
      out << " => ";
      out << render_const_value(vtype, v_iter->second);
      out << "," << endl;
    }

    out << ")";
  } else if (type->is_list() || type->is_set()) {
    t_type* etype;
    if (type->is_list()) {
      etype = ((t_list*)type)->get_elem_type();
    } else {
      etype = ((t_set*)type)->get_elem_type();
    }
    out << "[" << endl;
    const vector<t_const_value*>& val = value->get_list();
    vector<t_const_value*>::const_iterator v_iter;
    for (v_iter = val.begin(); v_iter != val.end(); ++v_iter) {

      out << render_const_value(etype, *v_iter);
      if (type->is_set()) {
        out << " => 1";
      }
      out << "," << endl;
    }
    out << "]";
  }
  return out.str();
}

/**
 * Make a struct
 */
void t_perl_generator::generate_struct(t_struct* tstruct) {
  generate_perl_struct(tstruct, false);
}

/**
 * Generates a struct definition for a thrift exception. Basically the same
 * as a struct but extends the Exception class.
 *
 * @param txception The struct definition
 */
void t_perl_generator::generate_xception(t_struct* txception) {
  generate_perl_struct(txception, true);
}

/**
 * Structs can be normal or exceptions.
 */
void t_perl_generator::generate_perl_struct(t_struct* tstruct,
                                            bool is_exception) {
  generate_perl_struct_definition(f_types_, tstruct, is_exception);
}

/**
 * Generates a struct definition for a thrift data type. This is nothing in PERL
 * where the objects are all just associative arrays (unless of course we
 * decide to start using objects for them...)
 *
 * @param tstruct The struct definition
 */
void t_perl_generator::generate_perl_struct_definition(ofstream& out,
                                                       t_struct* tstruct,
                                                       bool is_exception) {
  const vector<t_field*>& members = tstruct->get_members();
  vector<t_field*>::const_iterator m_iter;

  out <<
      "package " << perl_namespace(tstruct->get_program()) << tstruct->get_name() <<";\n";
  if (is_exception) {
    out << "use base('Thrift::TException');\n";
  }

  out << "sub new {\n";
  indent_up();
  out << "my $classname = shift;\n";
  out << "my $self      = {};\n";
  out << "my $vals      = shift || {};\n";

  for (m_iter = members.begin(); m_iter != members.end(); ++m_iter) {
    string dval = "undef";
    t_type* t = (*m_iter)->get_type();
    while (t->is_typedef()) {
      t = ((t_typedef*)t)->get_type();
    }
    if ((*m_iter)->get_value() != NULL && !(t->is_struct() || t->is_xception())) {
      dval = render_const_value((*m_iter)->get_type(), (*m_iter)->get_value());
    }
    out <<
      "$self->{" << (*m_iter)->get_name() << "} = " << dval << ";" << endl;
  }

  // Generate constructor from array
  if (members.size() > 0) {

    for (m_iter = members.begin(); m_iter != members.end(); ++m_iter) {
      t_type* t = (*m_iter)->get_type();
      while (t->is_typedef()) {
        t = ((t_typedef*)t)->get_type();
      }
      if ((*m_iter)->get_value() != NULL && (t->is_struct() || t->is_xception())) {
        indent(out) << "$self->{" << (*m_iter)->get_name() << "} = " << render_const_value(t, (*m_iter)->get_value()) << ";" << endl;
      }
    }

    out << indent() << "if (UNIVERSAL::isa($vals,'HASH')) {" << endl;
    indent_up();
    for (m_iter = members.begin(); m_iter != members.end(); ++m_iter) {
      out <<
        indent() << "if (defined $vals->{" << (*m_iter)->get_name() << "}) {" << endl <<
        indent() << "  $self->{" << (*m_iter)->get_name() << "} = $vals->{" << (*m_iter)->get_name() << "};" << endl <<
        indent() << "}" << endl;
    }
    indent_down();
    out <<
      indent() << "}" << endl;

  }

  out << "return bless($self,$classname);\n";
  indent_down();
  out << "}\n\n";

  out <<
    "sub getName {" << endl <<
    indent() << "  return '" << tstruct->get_name() << "';" << endl <<
    indent() << "}" << endl <<
    endl;

  generate_perl_struct_reader(out, tstruct);
  generate_perl_struct_writer(out, tstruct);

}

/**
 * Generates the read() method for a struct
 */
void t_perl_generator::generate_perl_struct_reader(ofstream& out,
                                                   t_struct* tstruct) {
  const vector<t_field*>& fields = tstruct->get_members();
  vector<t_field*>::const_iterator f_iter;

  out << "sub read {" <<endl;

  indent_up();

  out <<
    indent() << "my $self  = shift;" <<endl <<
    indent() << "my $input = shift;" <<endl <<
    indent() << "my $xfer  = 0;" << endl <<
    indent() << "my $fname;"     << endl <<
    indent() << "my $ftype = 0;" << endl <<
    indent() << "my $fid   = 0;" << endl;

  indent(out) << "$xfer += $input->readStructBegin(\\$fname);" << endl;


  // Loop over reading in fields
  indent(out) << "while (1) " << endl;

  scope_up(out);

  indent(out) << "$xfer += $input->readFieldBegin(\\$fname, \\$ftype, \\$fid);" << endl;

  // Check for field STOP marker and break
  indent(out) << "if ($ftype == TType::STOP) {" << endl;
  indent_up();
  indent(out) << "last;" << endl;
  indent_down();
  indent(out) << "}" << endl;

  // Switch statement on the field we are reading
  indent(out) << "SWITCH: for($fid)" << endl;

  scope_up(out);

  // Generate deserialization code for known cases
  for (f_iter = fields.begin(); f_iter != fields.end(); ++f_iter) {

    indent(out) << "/^" << (*f_iter)->get_key() << "$/ && do{";
    indent(out) << "if ($ftype == " << type_to_enum((*f_iter)->get_type()) << ") {" << endl;

    indent_up();
    generate_deserialize_field(out, *f_iter, "self->");
    indent_down();

    indent(out) << "} else {" << endl;

    indent(out) <<  "  $xfer += $input->skip($ftype);" << endl;

    out <<
      indent() << "}" << endl <<
      indent() << "last; };" << endl;

  }
  // In the default case we skip the field

  indent(out) <<  "  $xfer += $input->skip($ftype);" << endl;

  scope_down(out);

  indent(out) << "$xfer += $input->readFieldEnd();" << endl;

  scope_down(out);

  indent(out) << "$xfer += $input->readStructEnd();" << endl;

  indent(out) << "return $xfer;" << endl;

  indent_down();
  out << indent() << "}" << endl << endl;
}

/**
 * Generates the write() method for a struct
 */
void t_perl_generator::generate_perl_struct_writer(ofstream& out,
                                                   t_struct* tstruct) {
  string name = tstruct->get_name();
  const vector<t_field*>& fields = tstruct->get_members();
  vector<t_field*>::const_iterator f_iter;

  out << "sub write {" << endl;

  indent_up();
  indent(out) << "my $self   = shift;"<<endl;
  indent(out) << "my $output = shift;"<<endl;
  indent(out) << "my $xfer   = 0;" << endl;

  indent(out) << "$xfer += $output->writeStructBegin('" << name << "');" << endl;

  for (f_iter = fields.begin(); f_iter != fields.end(); ++f_iter) {
    out << indent() << "if (defined $self->{" << (*f_iter)->get_name() << "}) {" << endl;
    indent_up();

    indent(out) <<
      "$xfer += $output->writeFieldBegin(" <<
      "'" << (*f_iter)->get_name() << "', " <<
      type_to_enum((*f_iter)->get_type()) << ", " <<
      (*f_iter)->get_key() << ");" << endl;


    // Write field contents
    generate_serialize_field(out, *f_iter, "self->");

    indent(out) <<
        "$xfer += $output->writeFieldEnd();" << endl;

    indent_down();
    indent(out) << "}" << endl;
  }


  out <<
    indent() << "$xfer += $output->writeFieldStop();" << endl <<
    indent() << "$xfer += $output->writeStructEnd();" << endl;

  out <<indent() << "return $xfer;" << endl;

  indent_down();
  out <<
    indent() << "}" << endl <<
    endl;
}

/**
 * Generates a thrift service.
 *
 * @param tservice The service definition
 */
void t_perl_generator::generate_service(t_service* tservice) {
  string f_service_name = string(T_PERL_DIR)+"/"+service_name_+".pm";
  f_service_.open(f_service_name.c_str());

  f_service_ <<
    ///      "package "<<service_name_<<";"<<endl<<
    autogen_comment() <<
    perl_includes();

  f_service_ <<
    "use " << perl_namespace(tservice->get_program()) << "Types;" << endl;

  if (tservice->get_extends() != NULL) {
    f_service_ <<
      "use " << tservice->get_extends()->get_name() << ";" << endl;
  }

  f_service_ <<
    endl;

  // Generate the three main parts of the service (well, two for now in PERL)
  generate_service_helpers(tservice);
  generate_service_interface(tservice);
  generate_service_rest(tservice);
  generate_service_client(tservice);
  generate_service_processor(tservice);

  // Close service file
  f_service_ << "1;" << endl;
  f_service_.close();
}

/**
 * Generates a service server definition.
 *
 * @param tservice The service to generate a server for.
 */
void t_perl_generator::generate_service_processor(t_service* tservice) {
  // Generate the dispatch methods
  vector<t_function*> functions = tservice->get_functions();
  vector<t_function*>::iterator f_iter;

  string extends = "";
  string extends_processor = "";
  if (tservice->get_extends() != NULL) {
    extends = tservice->get_extends()->get_name();
    extends_processor = "use base('" + extends + "Processor');";
  }

  indent_up();

  // Generate the header portion
  f_service_ <<
      "package " << service_name_ << "Processor;" << endl << extends_processor << endl;


  if (extends.empty()) {
    f_service_ << "sub new {" << endl;

    indent_up();

    f_service_ <<
      indent() <<  "my $classname = shift;"<< endl <<
      indent() <<  "my $handler   = shift;"<< endl <<
      indent() <<  "my $self      = {};"   << endl;

    f_service_ <<
      indent() << "$self->{handler} = $handler;" << endl;

    f_service_ <<
      indent() << "return bless($self,$classname);"<<endl;

    indent_down();

    f_service_ <<"}" << endl << endl;
  }

  // Generate the server implementation
  f_service_ << "sub process {" << endl;
  indent_up();

  f_service_ <<
    indent() << "my $self   = shift;"<<endl <<
    indent() << "my $input  = shift;"<<endl <<
    indent() << "my $output = shift;"<<endl;

  f_service_ <<
    indent() << "my $rseqid = 0;" << endl <<
    indent() << "my $fname  = undef;" << endl <<
    indent() << "my $mtype  = 0;" << endl << endl;

  f_service_ <<
    indent() << "$input->readMessageBegin(\\$fname, \\$mtype, \\$rseqid);" << endl;

  // HOT: check for method implementation
  f_service_ <<
    indent() << "my $methodname = 'process_'.$fname;" << endl <<
    indent() << "if (!method_exists($self, $methodname)) {" << endl;

  f_service_ <<
    indent() << "  $input->skip(TType::STRUCT);" << endl <<
    indent() << "  $input->readMessageEnd();" << endl <<
    indent() << "  my $x = new TApplicationException('Function '.$fname.' not implemented.', TApplicationException::UNKNOWN_METHOD);" << endl <<
    indent() << "  $output->writeMessageBegin($fname, TMessageType::EXCEPTION, $rseqid);" << endl <<
    indent() << "  $x->write($output);" << endl <<
    indent() << "  $output->writeMessageEnd();" << endl <<
    indent() << "  $output->getTransport()->flush();" << endl <<
    indent() << "  return;" << endl;

  f_service_ <<
    indent() <<  "}" << endl <<
    indent() <<  "$self->$methodname($rseqid, $input, $output);" << endl <<
    indent() <<  "return 1;" << endl;

  indent_down();

  f_service_ <<
    indent() << "}" << endl <<endl;

  // Generate the process subfunctions
  for (f_iter = functions.begin(); f_iter != functions.end(); ++f_iter) {
    generate_process_function(tservice, *f_iter);
  }
}

/**
 * Generates a process function definition.
 *
 * @param tfunction The function to write a dispatcher for
 */
void t_perl_generator::generate_process_function(t_service* tservice,
                                                 t_function* tfunction) {
  // Open function
  f_service_ <<
    "sub process_" << tfunction->get_name() << "{"<<endl;

  indent_up();

  f_service_ <<
    indent() << "my $self = shift;"<<endl<<
    indent() << "my ($seqid, $input, $output); " << endl;

  string argsname = perl_namespace(tservice->get_program()) + service_name_ + "_" + tfunction->get_name() + "_args";
  string resultname = perl_namespace(tservice->get_program()) + service_name_ + "_" + tfunction->get_name() + "_result";

  f_service_ <<
    indent() << "my $args = new " << argsname << "();" << endl <<
    indent() << "$args->read($input);" << endl;

  f_service_ <<
    indent() << "$input->readMessageEnd();" << endl;

  t_struct* xs = tfunction->get_xceptions();
  const std::vector<t_field*>& xceptions = xs->get_members();
  vector<t_field*>::const_iterator x_iter;

  // Declare result for non async function
  if (!tfunction->is_async()) {
    f_service_ <<
      indent() << "my $result = new " << resultname << "();" << endl;
  }

  // Try block for a function with exceptions
  if (xceptions.size() > 0) {
    f_service_ <<
      indent() << "eval {" << endl;
    indent_up();
  }

  // Generate the function call
  t_struct* arg_struct = tfunction->get_arglist();
  const std::vector<t_field*>& fields = arg_struct->get_members();
  vector<t_field*>::const_iterator f_iter;

  f_service_ << indent();
  if (!tfunction->is_async() && !tfunction->get_returntype()->is_void()) {
    f_service_ << "$result->{success} = ";
  }
  f_service_ <<
    "$self->{handler}->" << tfunction->get_name() << "(";
  bool first = true;
  for (f_iter = fields.begin(); f_iter != fields.end(); ++f_iter) {
    if (first) {
      first = false;
    } else {
      f_service_ << ", ";
    }
    f_service_ << "$args->" << (*f_iter)->get_name();
  }
  f_service_ << ");" << endl;

  if (!tfunction->is_async() && xceptions.size() > 0) {
    indent_down();
    for (x_iter = xceptions.begin(); x_iter != xceptions.end(); ++x_iter) {
      f_service_ <<
        indent() << "}; if( UNIVERSAL::isa($@,'"<<(*x_iter)->get_type()->get_name()<<"') ){ "<<endl;

      if (!tfunction->is_async()) {
        indent_up();
        f_service_ <<
          indent() << "$result->{" << (*x_iter)->get_name() << "} = $@;" << endl;
        indent_down();
        f_service_ << indent();
      }
    }
    indent_down();
    f_service_ << "}" << endl;
  }

  // Shortcut out here for async functions
  if (tfunction->is_async()) {
    f_service_ <<
      indent() << "return;" << endl;
    indent_down();
    f_service_ <<
      indent() << "}" << endl;
    return;
  }
  indent_up();
  // Serialize the request header
  f_service_ <<
    indent() << "$output->writeMessageBegin('" << tfunction->get_name() << "', TMessageType::REPLY, $seqid);" << endl <<
    indent() << "$result->write($output);" << endl <<
    indent() << "$output->getTransport()->flush();" << endl;
  indent_down();

  // Close function
  indent_down();
  f_service_ <<
    indent() << "}" << endl;
}

/**
 * Generates helper functions for a service.
 *
 * @param tservice The service to generate a header definition for
 */
void t_perl_generator::generate_service_helpers(t_service* tservice) {
  vector<t_function*> functions = tservice->get_functions();
  vector<t_function*>::iterator f_iter;

  f_service_ <<
    "# HELPER FUNCTIONS AND STRUCTURES" << endl << endl;

  for (f_iter = functions.begin(); f_iter != functions.end(); ++f_iter) {
    t_struct* ts = (*f_iter)->get_arglist();
    string name = ts->get_name();
    ts->set_name(service_name_ + "_" + name);
    generate_perl_struct_definition(f_service_, ts, false);
    generate_perl_function_helpers(*f_iter);
    ts->set_name(name);
  }
}

/**
 * Generates a struct and helpers for a function.
 *
 * @param tfunction The function
 */
void t_perl_generator::generate_perl_function_helpers(t_function* tfunction) {
  t_struct result(program_, service_name_ + "_" + tfunction->get_name() + "_result");
  t_field success(tfunction->get_returntype(), "success", 0);
  if (!tfunction->get_returntype()->is_void()) {
    result.append(&success);
  }

  t_struct* xs = tfunction->get_xceptions();
  const vector<t_field*>& fields = xs->get_members();
  vector<t_field*>::const_iterator f_iter;
  for (f_iter = fields.begin(); f_iter != fields.end(); ++f_iter) {
    result.append(*f_iter);
  }

  generate_perl_struct_definition(f_service_, &result, false);
}

/**
 * Generates a service interface definition.
 *
 * @param tservice The service to generate a header definition for
 */
void t_perl_generator::generate_service_interface(t_service* tservice) {
  string extends = "";
  string extends_if = "";
  if (tservice->get_extends() != NULL) {
    extends = "use base('" + tservice->get_extends()->get_name() + "');";
    extends_if = "use base('" + tservice->get_extends()->get_name() + "If');";
  }

  f_service_ <<
    "package " << service_name_ << "If;"<<endl<<
    extends_if<<endl;


  indent_up();
  vector<t_function*> functions = tservice->get_functions();
  vector<t_function*>::iterator f_iter;
  for (f_iter = functions.begin(); f_iter != functions.end(); ++f_iter) {
    f_service_ <<
      "sub " << function_signature(*f_iter) <<endl<< "  die 'implement interface';\n}" << endl;
  }
  indent_down();

}

/**
 * Generates a REST interface
 */
void t_perl_generator::generate_service_rest(t_service* tservice) {
  string extends = "";
  string extends_if = "";
  if (tservice->get_extends() != NULL) {
    extends    =  tservice->get_extends()->get_name();
    extends_if = "use base('" + tservice->get_extends()->get_name() + "Rest');";
  }
  f_service_ <<
    "package " << service_name_ << "Rest;"<<endl<<
    extends_if << endl;


  if (extends.empty()) {
    f_service_ << "sub new {" << endl;

    indent_up();

    f_service_ <<
      indent() << "my $classname=shift;"<<endl <<
      indent() << "my $impl     =shift;"<<endl <<
      indent() << "my $self     ={ impl => $impl };"<<endl << endl<<
      indent() << "return bless($self,$classname);" << endl;


    indent_down();

    f_service_  <<
      indent() << "}" << endl << endl;
  }

  vector<t_function*> functions = tservice->get_functions();
  vector<t_function*>::iterator f_iter;
  for (f_iter = functions.begin(); f_iter != functions.end(); ++f_iter) {
    f_service_ <<
      "sub " << (*f_iter)->get_name() <<
      "{"    <<endl;

    indent_up();

    f_service_ <<
      indent() << "my $self = shift;"<< endl <<
      indent() << "my $request = shift;" << endl << endl;


    const vector<t_field*>& args = (*f_iter)->get_arglist()->get_members();
    vector<t_field*>::const_iterator a_iter;
    for (a_iter = args.begin(); a_iter != args.end(); ++a_iter) {
      t_type* atype = (*a_iter)->get_type();
      while (atype->is_typedef()) {
        atype = ((t_typedef*)atype)->get_type();
      }
      string req = "$request->{'" + (*a_iter)->get_name() + "'}";
      f_service_ <<
        indent() << "my $" << (*a_iter)->get_name() << " = (" << req << ") ? " << req << " : undef;" << endl;
      if (atype->is_string() &&
          ((t_base_type*)atype)->is_string_list()) {
        f_service_ <<
          indent() << "my @" << (*a_iter)->get_name() << " = split(/,/, $" << (*a_iter)->get_name() << ");" << endl <<
          indent()     << "$"<<(*a_iter)->get_name() <<" = \\@"<<(*a_iter)->get_name()<<endl;
      }
    }
    f_service_ <<
      indent() << "return $self->{impl}->" << (*f_iter)->get_name() << "(" << argument_list((*f_iter)->get_arglist()) << ");" << endl;
    indent_down();
    indent(f_service_) << "}" << endl <<endl;
  }

}

/**
 * Generates a service client definition.
 *
 * @param tservice The service to generate a server for.
 */
void t_perl_generator::generate_service_client(t_service* tservice) {
  string extends = "";
  string extends_client = "";
  if (tservice->get_extends() != NULL) {
    extends = tservice->get_extends()->get_name();
    extends_client = "use base('" + extends + "Client');";
  }

  f_service_ <<
      "package " << service_name_ << "Client;"<<endl;

  f_service_ <<
      extends_client << endl <<
      "use base('" << service_name_ << "If');" << endl;

  // Constructor function
  f_service_ << "sub new {"<<endl;

  indent_up();

  f_service_ <<
    indent() << "my $classname = shift;"<<endl<<
    indent() << "my $input     = shift;"<<endl<<
    indent() << "my $output    = shift;"<<endl<<
    indent() << "my $self      = {};"   <<endl;

  if (!extends.empty()) {
    f_service_ <<
      indent() << "  $self = $classname->SUPER::new($input, $output);" << endl;
  } else {
    f_service_ <<
      indent() << "  $self->{input}  = $input;" << endl <<
      indent() << "  $self->{output} = defined $output ? $output : $input;" << endl <<
      indent() << "  $self->{seqid}  = 0;" << endl;
  }

  f_service_ <<
    indent() << "return bless($self,$classname);"<<endl;

  indent_down();

  f_service_ <<
    indent() << "}" << endl << endl;

  // Generate client method implementations
  vector<t_function*> functions = tservice->get_functions();
  vector<t_function*>::const_iterator f_iter;
  for (f_iter = functions.begin(); f_iter != functions.end(); ++f_iter) {
    t_struct* arg_struct = (*f_iter)->get_arglist();
    const vector<t_field*>& fields = arg_struct->get_members();
    vector<t_field*>::const_iterator fld_iter;
    string funname = (*f_iter)->get_name();

    // Open function
    f_service_ << "sub " << function_signature(*f_iter) << endl;

    indent_up();

    indent(f_service_) << indent() <<
      "$self->send_" << funname << "(";

    bool first = true;
    for (fld_iter = fields.begin(); fld_iter != fields.end(); ++fld_iter) {
      if (first) {
        first = false;
      } else {
        f_service_ << ", ";
      }
      f_service_ << "$" << (*fld_iter)->get_name();
    }
    f_service_ << ");" << endl;

    if (!(*f_iter)->is_async()) {
      f_service_ << indent();
      if (!(*f_iter)->get_returntype()->is_void()) {
        f_service_ << "return ";
      }
      f_service_ <<
        "$self->recv_" << funname << "();" << endl;
    }

    indent_down();

    f_service_ << "}" << endl << endl;

    f_service_ <<
      "sub send_" << function_signature(*f_iter) << endl;

    indent_up();

    std::string argsname = perl_namespace(tservice->get_program()) + service_name_ + "_" + (*f_iter)->get_name() + "_args";

    // Serialize the request header
    f_service_ <<
      indent() << "$self->{output}->writeMessageBegin('" << (*f_iter)->get_name() << "', TMessageType::CALL, $self->{seqid});" << endl;

    f_service_ <<
      indent() << "my $args = new " << argsname << "();" << endl;

    for (fld_iter = fields.begin(); fld_iter != fields.end(); ++fld_iter) {
      f_service_ <<
        indent() << "$args->{" << (*fld_iter)->get_name() << "} = $" << (*fld_iter)->get_name() << ";" << endl;
    }

    // Write to the stream
    f_service_ <<
      indent() << "$args->write($self->{output});" << endl <<
      indent() << "$self->{output}->writeMessageEnd();" << endl <<
      indent() << "$self->{output}->getTransport()->flush();" << endl;


    indent_down();

    f_service_ << "}" << endl;


    if (!(*f_iter)->is_async()) {
      std::string resultname = perl_namespace(tservice->get_program()) + service_name_ + "_" + (*f_iter)->get_name() + "_result";
      t_struct noargs(program_);

      t_function recv_function((*f_iter)->get_returntype(),
                               string("recv_") + (*f_iter)->get_name(),
                               &noargs);
      // Open function
      f_service_ <<
        endl <<
        "sub " << function_signature(&recv_function) << endl;

      indent_up();

      f_service_ <<
        indent() << "my $rseqid = 0;" << endl <<
        indent() << "my $fname;" << endl <<
        indent() << "my $mtype = 0;" << endl <<
        endl;

      f_service_ <<
        indent() << "$self->{input}->readMessageBegin(\\$fname, \\$mtype, \\$rseqid);" << endl <<
        indent() << "if ($mtype == TMessageType::EXCEPTION) {" << endl <<
        indent() << "  my $x = new TApplicationException();" << endl <<
        indent() << "  $x->read($self->{input});" << endl <<
        indent() << "  $self->{input}->readMessageEnd();" << endl <<
        indent() << "  die $x;" << endl <<
        indent() << "}" << endl;


      f_service_ <<
        indent() << "my $result = new " << resultname << "();" << endl <<
        indent() << "$result->read($self->{input});" << endl;


      f_service_ <<
        indent() << "$self->{input}->readMessageEnd();" << endl <<
        endl;


      // Careful, only return result if not a void function
      if (!(*f_iter)->get_returntype()->is_void()) {
        f_service_ <<
          indent() << "if (defined $result->{success} ) {" << endl <<
          indent() << "  return $result->{success};" << endl <<
          indent() << "}" << endl;
      }

      t_struct* xs = (*f_iter)->get_xceptions();
      const std::vector<t_field*>& xceptions = xs->get_members();
      vector<t_field*>::const_iterator x_iter;
      for (x_iter = xceptions.begin(); x_iter != xceptions.end(); ++x_iter) {
        f_service_ <<
          indent() << "if (defined $result->{" << (*x_iter)->get_name() << "}) {" << endl <<
          indent() << "  die $result->{" << (*x_iter)->get_name() << "};" << endl <<
          indent() << "}" << endl;
      }

      // Careful, only return _result if not a void function
      if ((*f_iter)->get_returntype()->is_void()) {
        indent(f_service_) <<
          "return;" << endl;
      } else {
        f_service_ <<
          indent() << "die \"" << (*f_iter)->get_name() << " failed: unknown result\";" << endl;
      }

      // Close function
      indent_down();
      f_service_ << "}"<<endl;

    }
  }

}

/**
 * Deserializes a field of any type.
 */
void t_perl_generator::generate_deserialize_field(ofstream &out,
                                                  t_field* tfield,
                                                  string prefix,
                                                  bool inclass) {
  t_type* type = tfield->get_type();
  while (type->is_typedef()) {
    type = ((t_typedef*)type)->get_type();
  }

  if (type->is_void()) {
    throw "CANNOT GENERATE DESERIALIZE CODE FOR void TYPE: " +
      prefix + tfield->get_name();
  }

  string name = tfield->get_name();

  //Hack for when prefix is defined (always a hash ref)
  if (!prefix.empty()) {
    name = prefix + "{" + tfield->get_name() + "}";
  }

  if (type->is_struct() || type->is_xception()) {
    generate_deserialize_struct(out,
                                (t_struct*)type,
                                 name);
  } else if (type->is_container()) {
    generate_deserialize_container(out, type, name);
  } else if (type->is_base_type() || type->is_enum()) {
    indent(out) <<
      "$xfer += $input->";

    if (type->is_base_type()) {
      t_base_type::t_base tbase = ((t_base_type*)type)->get_base();
      switch (tbase) {
      case t_base_type::TYPE_VOID:
        throw "compiler error: cannot serialize void field in a struct: " +
          name;
        break;
      case t_base_type::TYPE_STRING:
        out << "readString(\\$" << name << ");";
        break;
      case t_base_type::TYPE_BOOL:
        out << "readBool(\\$" << name << ");";
        break;
      case t_base_type::TYPE_BYTE:
        out << "readByte(\\$" << name << ");";
        break;
      case t_base_type::TYPE_I16:
        out << "readI16(\\$" << name << ");";
        break;
      case t_base_type::TYPE_I32:
        out << "readI32(\\$" << name << ");";
        break;
      case t_base_type::TYPE_I64:
        out << "readI64(\\$" << name << ");";
        break;
      case t_base_type::TYPE_DOUBLE:
        out << "readDouble(\\$" << name << ");";
        break;
      default:
        throw "compiler error: no PERL name for base type " + tbase;
      }
    } else if (type->is_enum()) {
      out << "readI32(\\$" << name << ");";
    }
    out << endl;

  } else {
    printf("DO NOT KNOW HOW TO DESERIALIZE FIELD '%s' TYPE '%s'\n",
           tfield->get_name().c_str(), type->get_name().c_str());
  }
}

/**
 * Generates an unserializer for a variable. This makes two key assumptions,
 * first that there is a const char* variable named data that points to the
 * buffer for deserialization, and that there is a variable protocol which
 * is a reference to a TProtocol serialization object.
 */
void t_perl_generator::generate_deserialize_struct(ofstream &out,
                                                   t_struct* tstruct,
                                                   string prefix) {
  out <<
    indent() << "$" << prefix << " = new " << perl_namespace(tstruct->get_program()) << tstruct->get_name() << "();" << endl <<
    indent() << "$xfer += $" << prefix << "->read($input);" << endl;
}

void t_perl_generator::generate_deserialize_container(ofstream &out,
                                                      t_type* ttype,
                                                      string prefix) {
  scope_up(out);

  string size = tmp("_size");
  string ktype = tmp("_ktype");
  string vtype = tmp("_vtype");
  string etype = tmp("_etype");

  t_field fsize(g_type_i32, size);
  t_field fktype(g_type_byte, ktype);
  t_field fvtype(g_type_byte, vtype);
  t_field fetype(g_type_byte, etype);

  out <<
    indent() << "my $" << size << " = 0;" << endl;

  // Declare variables, read header
  if (ttype->is_map()) {
    out <<
      indent() << "$" << prefix << " = {};" << endl <<
      indent() << "my $" << ktype << " = 0;" << endl <<
      indent() << "my $" << vtype << " = 0;" << endl;

    out <<
      indent() << "$xfer += $input->readMapBegin(" <<
      "\\$" << ktype << ", \\$" << vtype << ", \\$" << size << ");" << endl;

  } else if (ttype->is_set()) {

    out <<
      indent() << "$" << prefix << " = {};" << endl <<
      indent() << "my $" << etype << " = 0;" << endl <<
      indent() << "$xfer += $input->readSetBegin(" <<
      "\\$" << etype << ", \\$" << size << ");" << endl;

  } else if (ttype->is_list()) {

    out <<
      indent() << "$" << prefix << " = [];" << endl <<
      indent() << "my $" << etype << " = 0;" << endl <<
      indent() << "$xfer += $input->readListBegin(" <<
      "\\$" << etype << ", \\$" << size << ");" << endl;

  }

  // For loop iterates over elements
  string i = tmp("_i");
  indent(out) <<
    "for (my $" <<
    i << " = 0; $" << i << " < $" << size << "; ++$" << i << ")" << endl;

  scope_up(out);

  if (ttype->is_map()) {
    generate_deserialize_map_element(out, (t_map*)ttype, prefix);
  } else if (ttype->is_set()) {
    generate_deserialize_set_element(out, (t_set*)ttype, prefix);
  } else if (ttype->is_list()) {
    generate_deserialize_list_element(out, (t_list*)ttype, prefix);
  }

  scope_down(out);


  // Read container end
  if (ttype->is_map()) {
    indent(out) << "$xfer += $input->readMapEnd();" << endl;
  } else if (ttype->is_set()) {
    indent(out) << "$xfer += $input->readSetEnd();" << endl;
  } else if (ttype->is_list()) {
    indent(out) << "$xfer += $input->readListEnd();" << endl;
  }

  scope_down(out);
}


/**
 * Generates code to deserialize a map
 */
void t_perl_generator::generate_deserialize_map_element(ofstream &out,
                                                        t_map* tmap,
                                                        string prefix) {
  string key = tmp("key");
  string val = tmp("val");
  t_field fkey(tmap->get_key_type(), key);
  t_field fval(tmap->get_val_type(), val);

  indent(out) <<
    declare_field(&fkey, true, true) << endl;
  indent(out) <<
    declare_field(&fval, true, true) << endl;

  generate_deserialize_field(out, &fkey);
  generate_deserialize_field(out, &fval);

  indent(out) <<
    "$" << prefix << "->{$" << key << "} = $" << val << ";" << endl;
}

void t_perl_generator::generate_deserialize_set_element(ofstream &out,
                                                        t_set* tset,
                                                        string prefix) {
  string elem = tmp("elem");
  t_field felem(tset->get_elem_type(), elem);

  indent(out) <<
    "my $" << elem << " = undef;" << endl;

  generate_deserialize_field(out, &felem);

  indent(out) <<
    "$" << prefix << "->{$" << elem << "} = 1;" << endl;
}

void t_perl_generator::generate_deserialize_list_element(ofstream &out,
                                                         t_list* tlist,
                                                         string prefix) {
  string elem = tmp("elem");
  t_field felem(tlist->get_elem_type(), elem);

  indent(out) <<
    "my $" << elem << " = undef;" << endl;

  generate_deserialize_field(out, &felem);

  indent(out) <<
    "push(@{$" << prefix << "},$" << elem << ");" << endl;
}


/**
 * Serializes a field of any type.
 *
 * @param tfield The field to serialize
 * @param prefix Name to prepend to field name
 */
void t_perl_generator::generate_serialize_field(ofstream &out,
                                                t_field* tfield,
                                                string prefix) {
  t_type* type = tfield->get_type();
  while (type->is_typedef()) {
    type = ((t_typedef*)type)->get_type();
  }

  // Do nothing for void types
  if (type->is_void()) {
    throw "CANNOT GENERATE SERIALIZE CODE FOR void TYPE: " +
      prefix + tfield->get_name();
  }

  if (type->is_struct() || type->is_xception()) {
    generate_serialize_struct(out,
                              (t_struct*)type,
                               prefix + "{"+tfield->get_name()+"}" );
  } else if (type->is_container()) {
    generate_serialize_container(out,
                                 type,
                                 prefix + "{" + tfield->get_name()+"}");
  } else if (type->is_base_type() || type->is_enum()) {

    string name = tfield->get_name();

    //Hack for when prefix is defined (always a hash ref)
    if(!prefix.empty())
      name = prefix + "{" + tfield->get_name() + "}";

    indent(out) <<
      "$xfer += $output->";

    if (type->is_base_type()) {
      t_base_type::t_base tbase = ((t_base_type*)type)->get_base();
      switch (tbase) {
      case t_base_type::TYPE_VOID:
        throw
          "compiler error: cannot serialize void field in a struct: " + name;
        break;
      case t_base_type::TYPE_STRING:
        out << "writeString($" << name << ");";
        break;
      case t_base_type::TYPE_BOOL:
        out << "writeBool($" << name << ");";
        break;
      case t_base_type::TYPE_BYTE:
        out << "writeByte($" << name << ");";
        break;
      case t_base_type::TYPE_I16:
        out << "writeI16($" << name << ");";
        break;
      case t_base_type::TYPE_I32:
        out << "writeI32($" << name << ");";
        break;
      case t_base_type::TYPE_I64:
        out << "writeI64($" << name << ");";
        break;
      case t_base_type::TYPE_DOUBLE:
        out << "writeDouble($" << name << ");";
        break;
      default:
        throw "compiler error: no PERL name for base type " + tbase;
      }
    } else if (type->is_enum()) {
      out << "writeI32($" << name << ");";
    }
    out << endl;

  } else {
    printf("DO NOT KNOW HOW TO SERIALIZE FIELD '%s%s' TYPE '%s'\n",
           prefix.c_str(),
           tfield->get_name().c_str(),
           type->get_name().c_str());
  }
}

/**
 * Serializes all the members of a struct.
 *
 * @param tstruct The struct to serialize
 * @param prefix  String prefix to attach to all fields
 */
void t_perl_generator::generate_serialize_struct(ofstream &out,
                                                 t_struct* tstruct,
                                                 string prefix) {
    indent(out) <<
      "$xfer += $" << prefix << "->write($output);" << endl;
}

/**
 * Writes out a container
 */
void t_perl_generator::generate_serialize_container(ofstream &out,
                                                    t_type* ttype,
                                                    string prefix) {
  scope_up(out);

  if (ttype->is_map()) {
    indent(out) <<
      "$output->writeMapBegin(" <<
      type_to_enum(((t_map*)ttype)->get_key_type()) << ", " <<
      type_to_enum(((t_map*)ttype)->get_val_type()) << ", " <<
      "scalar(keys %{$" << prefix << "}));" << endl;
  } else if (ttype->is_set()) {
    indent(out) <<
      "$output->writeSetBegin(" <<
      type_to_enum(((t_set*)ttype)->get_elem_type()) << ", " <<
      "scalar(@{$" << prefix << "}));" << endl;

  } else if (ttype->is_list()) {

    indent(out) <<
      "$output->writeListBegin(" <<
      type_to_enum(((t_list*)ttype)->get_elem_type()) << ", " <<
      "scalar(@{$" << prefix << "}));" << endl;

  }

  scope_up(out);

  if (ttype->is_map()) {
    string kiter = tmp("kiter");
    string viter = tmp("viter");
    indent(out) <<
      "while( my ($"<<kiter<<",$"<<viter<<") = each %{$" << prefix << "}) " << endl;

    scope_up(out);
    generate_serialize_map_element(out, (t_map*)ttype, kiter, viter);
    scope_down(out);

  } else if (ttype->is_set()) {
    string iter = tmp("iter");
    indent(out) <<
      "foreach my $"<<iter<<" (@{$" << prefix << "})" << endl;
    scope_up(out);
    generate_serialize_set_element(out, (t_set*)ttype, iter);
    scope_down(out);


  } else if (ttype->is_list()) {
    string iter = tmp("iter");
    indent(out) <<
      "foreach my $"<<iter<<" (@{$" << prefix << "}) " << endl;
    scope_up(out);
    generate_serialize_list_element(out, (t_list*)ttype, iter);
    scope_down(out);
  }

  scope_down(out);

  if (ttype->is_map()) {
    indent(out) <<
      "$output->writeMapEnd();" << endl;
  } else if (ttype->is_set()) {
    indent(out) <<
      "$output->writeSetEnd();" << endl;
  } else if (ttype->is_list()) {
    indent(out) <<
      "$output->writeListEnd();" << endl;
  }

  scope_down(out);
}

/**
 * Serializes the members of a map.
 *
 */
void t_perl_generator::generate_serialize_map_element(ofstream &out,
                                                      t_map* tmap,
                                                      string kiter,
                                                      string viter) {
  t_field kfield(tmap->get_key_type(), kiter);
  generate_serialize_field(out, &kfield);

  t_field vfield(tmap->get_val_type(), viter);
  generate_serialize_field(out, &vfield);
}

/**
 * Serializes the members of a set.
 */
void t_perl_generator::generate_serialize_set_element(ofstream &out,
                                                      t_set* tset,
                                                      string iter) {
  t_field efield(tset->get_elem_type(), iter);
  generate_serialize_field(out, &efield);
}

/**
 * Serializes the members of a list.
 */
void t_perl_generator::generate_serialize_list_element(ofstream &out,
                                                       t_list* tlist,
                                                       string iter) {
  t_field efield(tlist->get_elem_type(), iter);
  generate_serialize_field(out, &efield);
}

/**
 * Declares a field, which may include initialization as necessary.
 *
 * @param ttype The type
 */
string t_perl_generator::declare_field(t_field* tfield, bool init, bool obj) {
  string result = "my $" + tfield->get_name();
  if (init) {
    t_type* type = tfield->get_type();
    while (type->is_typedef()) {
      type = ((t_typedef*)type)->get_type();
    }
    if (type->is_base_type()) {
      t_base_type::t_base tbase = ((t_base_type*)type)->get_base();
      switch (tbase) {
      case t_base_type::TYPE_VOID:
        break;
      case t_base_type::TYPE_STRING:
        result += " = ''";
        break;
      case t_base_type::TYPE_BOOL:
        result += " = 0";
        break;
      case t_base_type::TYPE_BYTE:
      case t_base_type::TYPE_I16:
      case t_base_type::TYPE_I32:
      case t_base_type::TYPE_I64:
        result += " = 0";
        break;
      case t_base_type::TYPE_DOUBLE:
        result += " = 0.0";
        break;
      default:
        throw "compiler error: no PERL initializer for base type " + tbase;
      }
    } else if (type->is_enum()) {
      result += " = 0";
    } else if (type->is_container()) {
      result += " = []";
    } else if (type->is_struct() || type->is_xception()) {
      if (obj) {
        result += " = new " + perl_namespace(type->get_program()) + type->get_name() + "()";
      } else {
        result += " = undef";
      }
    }
  }
  return result + ";";
}

/**
 * Renders a function signature of the form 'type name(args)'
 *
 * @param tfunction Function definition
 * @return String of rendered function definition
 */
string t_perl_generator::function_signature(t_function* tfunction,
                                            string prefix) {

  string str;

  str  = prefix + tfunction->get_name() + "{\n";
  str += "  my $self = shift;\n";

  //Need to create perl function arg inputs
  const vector<t_field*> &fields = tfunction->get_arglist()->get_members();
  vector<t_field*>::const_iterator f_iter;

  for (f_iter = fields.begin(); f_iter != fields.end(); ++f_iter) {
    str += "  my $" + (*f_iter)->get_name() + " = shift;\n";
  }

  return str;
}

/**
 * Renders a field list
 */
string t_perl_generator::argument_list(t_struct* tstruct) {
  string result = "";

  const vector<t_field*>& fields = tstruct->get_members();
  vector<t_field*>::const_iterator f_iter;
  bool first = true;
  for (f_iter = fields.begin(); f_iter != fields.end(); ++f_iter) {
    if (first) {
      first = false;
    } else {
      result += ", ";
    }
    result += "$" + (*f_iter)->get_name();
  }
  return result;
}

/**
 * Converts the parse type to a C++ enum string for the given type.
 */
string t_perl_generator ::type_to_enum(t_type* type) {
  while (type->is_typedef()) {
    type = ((t_typedef*)type)->get_type();
  }

  if (type->is_base_type()) {
    t_base_type::t_base tbase = ((t_base_type*)type)->get_base();
    switch (tbase) {
    case t_base_type::TYPE_VOID:
      throw "NO T_VOID CONSTRUCT";
    case t_base_type::TYPE_STRING:
      return "TType::STRING";
    case t_base_type::TYPE_BOOL:
      return "TType::BOOL";
    case t_base_type::TYPE_BYTE:
      return "TType::BYTE";
    case t_base_type::TYPE_I16:
      return "TType::I16";
    case t_base_type::TYPE_I32:
      return "TType::I32";
    case t_base_type::TYPE_I64:
      return "TType::I64";
    case t_base_type::TYPE_DOUBLE:
      return "TType::DOUBLE";
    }
  } else if (type->is_enum()) {
    return "TType::I32";
  } else if (type->is_struct() || type->is_xception()) {
    return "TType::STRUCT";
  } else if (type->is_map()) {
    return "TType::MAP";
  } else if (type->is_set()) {
    return "TType::SET";
  } else if (type->is_list()) {
    return "TType::LIST";
  }

  throw "INVALID TYPE IN type_to_enum: " + type->get_name();
}
