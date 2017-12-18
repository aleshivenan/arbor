#include <algorithm>
#include <string>
#include <unordered_set>

#include "cprinter.hpp"
#include "lexer.hpp"
#include "options.hpp"

/******************************************************************************
                              CPrinter driver
******************************************************************************/

CPrinter::CPrinter(Module &m, bool o)
    : module_(&m),
      optimize_(o)
{ }

std::string CPrinter::emit_source() {
    // make a list of vector types, both parameters and assigned
    // and a list of all scalar types
    std::vector<VariableExpression*> scalar_variables;
    std::vector<VariableExpression*> array_variables;

    for(auto& sym: module_->symbols()) {
        if(auto var = sym.second->is_variable()) {
            if(var->is_range()) {
                array_variables.push_back(var);
            }
            else {
                scalar_variables.push_back(var);
            }
        }
    }

    std::string module_name = Options::instance().modulename;
    if (module_name == "") {
        module_name = module_->name();
    }

    //////////////////////////////////////////////
    //////////////////////////////////////////////
    emit_headers();

    //////////////////////////////////////////////
    //////////////////////////////////////////////
    std::string class_name = "mechanism_" + module_name;

    text_.add_line("namespace arb { namespace multicore {");
    text_.add_line();
    text_.add_line("template<class Backend>");
    text_.add_line("class " + class_name + " : public mechanism<Backend> {");
    text_.add_line("public:");
    text_.increase_indentation();
    text_.add_line("using base = mechanism<Backend>;");
    text_.add_line("using value_type  = typename base::value_type;");
    text_.add_line("using size_type   = typename base::size_type;");
    text_.add_line();
    text_.add_line("using array = typename base::array;");
    text_.add_line("using iarray  = typename base::iarray;");
    text_.add_line("using view   = typename base::view;");
    text_.add_line("using iview  = typename base::iview;");
    text_.add_line("using const_view = typename base::const_view;");
    text_.add_line("using const_iview = typename base::const_iview;");
    text_.add_line("using ion_type = typename base::ion_type;");
    text_.add_line("using deliverable_event_stream_state = typename base::deliverable_event_stream_state;");
    text_.add_line();

    //////////////////////////////////////////////
    //////////////////////////////////////////////
    for(auto& ion: module_->neuron_block().ions) {
        auto tname = "Ion" + ion.name;
        text_.add_line("struct " + tname + " {");
        text_.increase_indentation();
        for(auto& field : ion.read) {
            text_.add_line("view " + field.spelling + ";");
        }
        for(auto& field : ion.write) {
            text_.add_line("view " + field.spelling + ";");
        }
        text_.add_line("iarray index;");
        text_.add_line("std::size_t memory() const { return sizeof(size_type)*index.size(); }");
        text_.add_line("std::size_t size() const { return index.size(); }");
        text_.decrease_indentation();
        text_.add_line("};");
        text_.add_line(tname + " ion_" + ion.name + ";");
    }

    //////////////////////////////////////////////
    // constructor
    //////////////////////////////////////////////
    int num_vars = array_variables.size();
    text_.add_line();
    text_.add_line(class_name + "(size_type mech_id, const_iview vec_ci, const_view vec_t, const_view vec_t_to, const_view vec_dt, view vec_v, view vec_i, array&& weights, iarray&& node_index)");
    text_.add_line(":   base(mech_id, vec_ci, vec_t, vec_t_to, vec_dt, vec_v, vec_i, std::move(node_index))");
    text_.add_line("{");
    text_.increase_indentation();
    text_.add_gutter() << "size_type num_fields = " << num_vars << ";";
    text_.end_line();

    text_.add_line();
    text_.add_line("// calculate the padding required to maintain proper alignment of sub arrays");
    text_.add_line("auto alignment  = data_.alignment();");
    text_.add_line("auto field_size_in_bytes = sizeof(value_type)*size();");
    text_.add_line("auto remainder  = field_size_in_bytes % alignment;");
    text_.add_line("auto padding    = remainder ? (alignment - remainder)/sizeof(value_type) : 0;");
    text_.add_line("auto field_size = size()+padding;");

    text_.add_line();
    text_.add_line("// allocate memory");
    text_.add_line("data_ = array(field_size*num_fields, std::numeric_limits<value_type>::quiet_NaN());");

    // assign the sub-arrays
    // replace this : data_(1*n, 2*n);
    //    with this : data_(1*field_size, 1*field_size+n);

    text_.add_line();
    text_.add_line("// asign the sub-arrays");
    for(int i=0; i<num_vars; ++i) {
        char namestr[128];
        sprintf(namestr, "%-15s", array_variables[i]->name().c_str());
        if(optimize_) {
            text_.add_gutter() << namestr << " = data_.data() + "
                               << i << "*field_size;";
        }
        else {
            text_.add_gutter() << namestr << " = data_("
                               << i << "*field_size, " << i+1 << "*size());";
        }
        text_.end_line();
    }
    text_.add_line();

    // copy in the weights
    text_.add_line("// add the user-supplied weights for converting from current density");
    text_.add_line("// to per-compartment current in nA");
    text_.add_line("if (weights.size()) {");
    text_.increase_indentation();
    if(optimize_) {
        text_.add_line("memory::copy(weights, view(weights_, size()));");
    }
    else {
        text_.add_line("memory::copy(weights, weights_(0, size()));");
    }
    text_.decrease_indentation();
    text_.add_line("}");
    text_.add_line("else {");
    text_.increase_indentation();
    text_.add_line("memory::fill(weights_, 1.0);");
    text_.decrease_indentation();
    text_.add_line("}");
    text_.add_line();

    text_.add_line("// set initial values for variables and parameters");
    for(auto const& var : array_variables) {
        double val = var->value();
        // only non-NaN fields need to be initialized, because data_
        // is NaN by default
        std::string pointer_name = var->name();
        if(!optimize_) pointer_name += ".data()";
        if(val == val) {
            text_.add_gutter() << "std::fill(" << pointer_name << ", "
                                               << pointer_name << "+size(), "
                                               << val << ");";
            text_.end_line();
        }
    }

    text_.add_line();
    text_.decrease_indentation();
    text_.add_line("}");

    //////////////////////////////////////////////
    //////////////////////////////////////////////

    text_.add_line();
    text_.add_line("using base::size;");
    text_.add_line();

    text_.add_line("std::size_t memory() const override {");
    text_.increase_indentation();
    text_.add_line("auto s = std::size_t{0};");
    text_.add_line("s += data_.size()*sizeof(value_type);");
    for(auto& ion: module_->neuron_block().ions) {
        text_.add_line("s += ion_" + ion.name + ".memory();");
    }
    text_.add_line("return s;");
    text_.decrease_indentation();
    text_.add_line("}");
    text_.add_line();

    text_.add_line("std::string name() const override {");
    text_.increase_indentation();
    text_.add_line("return \"" + module_name + "\";");
    text_.decrease_indentation();
    text_.add_line("}");
    text_.add_line();

    std::string kind_str = module_->kind() == moduleKind::density
                            ? "mechanismKind::density"
                            : "mechanismKind::point";
    text_.add_line("mechanismKind kind() const override {");
    text_.increase_indentation();
    text_.add_line("return " + kind_str + ";");
    text_.decrease_indentation();
    text_.add_line("}");
    text_.add_line();

    // Implement `set_weights` method.
    text_.add_line("void set_weights(array&& weights) override {");
    text_.increase_indentation();
    text_.add_line("memory::copy(weights, weights_(0, size()));");
    text_.decrease_indentation();
    text_.add_line("}");
    text_.add_line();

    // return true/false indicating if cell has dependency on k
    auto const& ions = module_->neuron_block().ions;
    auto find_ion = [&ions] (ionKind k) {
        return std::find_if(
            ions.begin(), ions.end(),
            [k](IonDep const& d) {return d.kind()==k;}
        );
    };
    auto has_ion = [&ions, find_ion] (ionKind k) {
        return find_ion(k) != ions.end();
    };

    // bool uses_ion(ionKind k) const override
    text_.add_line("bool uses_ion(ionKind k) const override {");
    text_.increase_indentation();
    text_.add_line("switch(k) {");
    text_.increase_indentation();
    text_.add_gutter()
        << "case ionKind::na : return "
        << (has_ion(ionKind::Na) ? "true" : "false") << ";";
    text_.end_line();
    text_.add_gutter()
        << "case ionKind::ca : return "
        << (has_ion(ionKind::Ca) ? "true" : "false") << ";";
    text_.end_line();
    text_.add_gutter()
        << "case ionKind::k  : return "
        << (has_ion(ionKind::K) ? "true" : "false") << ";";
    text_.end_line();
    text_.decrease_indentation();
    text_.add_line("}");
    text_.add_line("return false;");
    text_.decrease_indentation();
    text_.add_line("}");
    text_.add_line();

    /***************************************************************************
     *
     *   ion channels have the following fields :
     *
     *       ---------------------------------------------------
     *       label   Ca      Na      K   name
     *       ---------------------------------------------------
     *       iX      ica     ina     ik  current
     *       eX      eca     ena     ek  reversal_potential
     *       Xi      cai     nai     ki  internal_concentration
     *       Xo      cao     nao     ko  external_concentration
     *       gX      gca     gna     gk  conductance
     *       ---------------------------------------------------
     *
     **************************************************************************/

    // void set_ion(ionKind k, ion_type& i) override
    //      TODO: this is done manually, which isn't going to scale
    auto has_variable = [] (IonDep const& ion, std::string const& name) {
        if( std::find_if(ion.read.begin(), ion.read.end(),
                      [&name] (Token const& t) {return t.spelling==name;}
            ) != ion.read.end()
        ) return true;
        if( std::find_if(ion.write.begin(), ion.write.end(),
                      [&name] (Token const& t) {return t.spelling==name;}
            ) != ion.write.end()
        ) return true;
        return false;
    };
    text_.add_line("void set_ion(ionKind k, ion_type& i, std::vector<size_type>const& index) override {");
    text_.increase_indentation();
    text_.add_line("using arb::algorithms::index_into;");
    if(has_ion(ionKind::Na)) {
        auto ion = find_ion(ionKind::Na);
        text_.add_line("if(k==ionKind::na) {");
        text_.increase_indentation();
        text_.add_line("ion_na.index = iarray(memory::make_const_view(index));");
        if(has_variable(*ion, "ina")) text_.add_line("ion_na.ina = i.current();");
        if(has_variable(*ion, "ena")) text_.add_line("ion_na.ena = i.reversal_potential();");
        if(has_variable(*ion, "nai")) text_.add_line("ion_na.nai = i.internal_concentration();");
        if(has_variable(*ion, "nao")) text_.add_line("ion_na.nao = i.external_concentration();");
        text_.add_line("return;");
        text_.decrease_indentation();
        text_.add_line("}");
    }
    if(has_ion(ionKind::Ca)) {
        auto ion = find_ion(ionKind::Ca);
        text_.add_line("if(k==ionKind::ca) {");
        text_.increase_indentation();
        text_.add_line("ion_ca.index = iarray(memory::make_const_view(index));");
        if(has_variable(*ion, "ica")) text_.add_line("ion_ca.ica = i.current();");
        if(has_variable(*ion, "eca")) text_.add_line("ion_ca.eca = i.reversal_potential();");
        if(has_variable(*ion, "cai")) text_.add_line("ion_ca.cai = i.internal_concentration();");
        if(has_variable(*ion, "cao")) text_.add_line("ion_ca.cao = i.external_concentration();");
        text_.add_line("return;");
        text_.decrease_indentation();
        text_.add_line("}");
    }
    if(has_ion(ionKind::K)) {
        auto ion = find_ion(ionKind::K);
        text_.add_line("if(k==ionKind::k) {");
        text_.increase_indentation();
        text_.add_line("ion_k.index = iarray(memory::make_const_view(index));");
        if(has_variable(*ion, "ik")) text_.add_line("ion_k.ik = i.current();");
        if(has_variable(*ion, "ek")) text_.add_line("ion_k.ek = i.reversal_potential();");
        if(has_variable(*ion, "ki")) text_.add_line("ion_k.ki = i.internal_concentration();");
        if(has_variable(*ion, "ko")) text_.add_line("ion_k.ko = i.external_concentration();");
        text_.add_line("return;");
        text_.decrease_indentation();
        text_.add_line("}");
    }
    text_.add_line("throw std::domain_error(arb::util::pprintf(\"mechanism % does not support ion type\\n\", name()));");
    text_.decrease_indentation();
    text_.add_line("}");
    text_.add_line();

    //////////////////////////////////////////////
    //////////////////////////////////////////////

    auto proctest = [] (procedureKind k) {
        return is_in(k, {procedureKind::normal, procedureKind::api, procedureKind::net_receive});
    };
    bool override_deliver_events = false;
    for(auto const& var: module_->symbols()) {
        auto isproc = var.second->kind()==symbolKind::procedure;
        if(isproc) {
            auto proc = var.second->is_procedure();
            if(proctest(proc->kind())) {
                proc->accept(this);
            }
            override_deliver_events |= proc->kind()==procedureKind::net_receive;
        }
    }

    if(override_deliver_events) {
        text_.add_line("void deliver_events(const deliverable_event_stream_state& events) override {");
        text_.increase_indentation();
        text_.add_line("auto ncell = events.n_streams();");
        text_.add_line("for (size_type c = 0; c<ncell; ++c) {");
        text_.increase_indentation();

        text_.add_line("auto begin = events.begin_marked(c);");
        text_.add_line("auto end = events.end_marked(c);");
        text_.add_line("for (auto p = begin; p<end; ++p) {");
        text_.increase_indentation();
        text_.add_line("if (p->mech_id==mech_id_) net_receive(p->mech_index, p->weight);");
        text_.decrease_indentation();
        text_.add_line("}");
        text_.decrease_indentation();
        text_.add_line("}");
        text_.decrease_indentation();
        text_.add_line("}");
        text_.add_line();
    }

    // TODO: replace field_info() generation implemenation with separate schema info generation
    // as per #349.
    auto field_info_string = [](const std::string& kind, const Id& id) {
        return  "field_spec{field_spec::" + kind + ", " +
                "\"" + id.unit_string() + "\", " +
                (id.has_value()? id.value: "0") +
                (id.has_range()? ", " + id.range.first.spelling + "," + id.range.second.spelling: "") +
                "}";
    };

    std::unordered_set<std::string> scalar_set;
    for (auto& v: scalar_variables) {
        scalar_set.insert(v->name());
    }

    std::vector<Id> global_param_ids;
    std::vector<Id> instance_param_ids;

    for (const Id& id: module_->parameter_block().parameters) {
        auto var = id.token.spelling;
        (scalar_set.count(var)? global_param_ids: instance_param_ids).push_back(id);
    }
    const std::vector<Id>& state_ids = module_->state_block().state_variables;

    text_.add_line("util::optional<field_spec> field_info(const char* id) const /* override */ {");
    text_.increase_indentation();
    text_.add_line("static const std::pair<const char*, field_spec> field_tbl[] = {");
    text_.increase_indentation();
    for (const auto& id: global_param_ids) {
        auto var = id.token.spelling;
        text_.add_line("{\""+var+"\", "+field_info_string("global",id )+"},");
    }
    for (const auto& id: instance_param_ids) {
        auto var = id.token.spelling;
        text_.add_line("{\""+var+"\", "+field_info_string("parameter", id)+"},");
    }
    for (const auto& id: state_ids) {
        auto var = id.token.spelling;
        text_.add_line("{\""+var+"\", "+field_info_string("state", id)+"},");
    }
    text_.decrease_indentation();
    text_.add_line("};");
    text_.add_line();
    text_.add_line("auto* info = util::table_lookup(field_tbl, id);");
    text_.add_line("return info? util::just(*info): util::nothing;");
    text_.decrease_indentation();
    text_.add_line("}");
    text_.add_line();

    if (!instance_param_ids.empty() || !state_ids.empty()) {
        text_.add_line("view base::* field_view_ptr(const char* id) const override {");
        text_.increase_indentation();
        text_.add_line("static const std::pair<const char*, view "+class_name+"::*> field_tbl[] = {");
        text_.increase_indentation();
        for (const auto& id: instance_param_ids) {
            auto var = id.token.spelling;
            text_.add_line("{\""+var+"\", &"+class_name+"::"+var+"},");
        }
        for (const auto& id: state_ids) {
            auto var = id.token.spelling;
            text_.add_line("{\""+var+"\", &"+class_name+"::"+var+"},");
        }
        text_.decrease_indentation();
        text_.add_line("};");
        text_.add_line();
        text_.add_line("auto* pptr = util::table_lookup(field_tbl, id);");
        text_.add_line("return pptr? static_cast<view base::*>(*pptr): nullptr;");
        text_.decrease_indentation();
        text_.add_line("}");
        text_.add_line();
    }

    if (!global_param_ids.empty()) {
        text_.add_line("value_type base::* field_value_ptr(const char* id) const override {");
        text_.increase_indentation();
        text_.add_line("static const std::pair<const char*, value_type "+class_name+"::*> field_tbl[] = {");
        text_.increase_indentation();
        for (const auto& id: global_param_ids) {
            auto var = id.token.spelling;
            text_.add_line("{\""+var+"\", &"+class_name+"::"+var+"},");
        }
        text_.decrease_indentation();
        text_.add_line("};");
        text_.add_line();
        text_.add_line("auto* pptr = util::table_lookup(field_tbl, id);");
        text_.add_line("return pptr? static_cast<value_type base::*>(*pptr): nullptr;");
        text_.decrease_indentation();
        text_.add_line("}");
        text_.add_line();
    }

    //////////////////////////////////////////////
    //////////////////////////////////////////////

    text_.add_line("array data_;");
    for(auto var: array_variables) {
        if(optimize_) {
            text_.add_line("value_type *" + var->name() + ";");
        }
        else {
            text_.add_line("view " + var->name() + ";");
        }
    }

    for(auto var: scalar_variables) {
        double val = var->value();
        // test the default value for NaN
        // useful for error propogation from bad initial conditions
        if(val==val) {
            text_.add_gutter() << "value_type " << var->name() << " = " << val << ";";
            text_.end_line();
        }
        else {
            text_.add_line("value_type " + var->name() + " = 0;");
        }
    }

    text_.add_line();
    text_.add_line("using base::mech_id_;");
    text_.add_line("using base::vec_ci_;");
    text_.add_line("using base::vec_t_;");
    text_.add_line("using base::vec_t_to_;");
    text_.add_line("using base::vec_dt_;");
    text_.add_line("using base::vec_v_;");
    text_.add_line("using base::vec_i_;");
    text_.add_line("using base::node_index_;");

    text_.add_line();
    text_.decrease_indentation();
    text_.add_line("};");
    text_.add_line();

    text_.add_line("}} // namespaces");
    return text_.str();
}



void CPrinter::emit_headers() {
    text_.add_line("#pragma once");
    text_.add_line();
    text_.add_line("#include <cmath>");
    text_.add_line("#include <limits>");
    text_.add_line();
    text_.add_line("#include <mechanism.hpp>");
    text_.add_line("#include <algorithms.hpp>");
    text_.add_line("#include <backends/event.hpp>");
    text_.add_line("#include <backends/multi_event_stream_state.hpp>");
    text_.add_line("#include <util/pprintf.hpp>");
    text_.add_line("#include <util/simple_table.hpp>");
    text_.add_line();
}

/******************************************************************************
                              CPrinter
******************************************************************************/

void CPrinter::visit(Expression *e) {
    throw compiler_exception(
        "CPrinter doesn't know how to print " + e->to_string(),
        e->location());
}

void CPrinter::visit(LocalDeclaration *e) {
}

void CPrinter::visit(Symbol *e) {
    throw compiler_exception("I don't know how to print raw Symbol " + e->to_string(),
                             e->location());
}

void CPrinter::visit(LocalVariable *e) {
    std::string const& name = e->name();
    text_ << name;
    if(is_ghost_local(e)) {
        text_ << "[j_]";
    }
}

void CPrinter::visit(NumberExpression *e) {
    text_ << " " << e->value();
}

void CPrinter::visit(IdentifierExpression *e) {
    e->symbol()->accept(this);
}

void CPrinter::visit(VariableExpression *e) {
    text_ << e->name();
    if(e->is_range()) {
        text_ << "[i_]";
    }
}

void CPrinter::visit(IndexedVariable *e) {
    text_ << e->index_name() << "[i_]";
}

void CPrinter::visit(CellIndexedVariable *e) {
    text_ << e->index_name() << "[i_]";
}

void CPrinter::visit(UnaryExpression *e) {
    auto b = (e->expression()->is_binary()!=nullptr);
    switch(e->op()) {
        case tok::minus :
            // place a space in front of minus sign to avoid invalid
            // expressions of the form : (v[i]--67)
            if(b) text_ << " -(";
            else  text_ << " -";
            e->expression()->accept(this);
            if(b) text_ << ")";
            return;
        case tok::exp :
            text_ << "exp(";
            e->expression()->accept(this);
            text_ << ")";
            return;
        case tok::cos :
            text_ << "cos(";
            e->expression()->accept(this);
            text_ << ")";
            return;
        case tok::sin :
            text_ << "sin(";
            e->expression()->accept(this);
            text_ << ")";
            return;
        case tok::log :
            text_ << "log(";
            e->expression()->accept(this);
            text_ << ")";
            return;
        default :
            throw compiler_exception(
                "CPrinter unsupported unary operator " + yellow(token_string(e->op())),
                e->location());
    }
}

void CPrinter::visit(BlockExpression *e) {
    // ------------- declare local variables ------------- //
    // only if this is the outer block
    if(!e->is_nested()) {
        std::vector<std::string> names;
        for(auto& symbol : e->scope()->locals()) {
            auto sym = symbol.second.get();
            // input variables are declared earlier, before the
            // block body is printed
            if(is_stack_local(sym) && !is_input(sym)) {
                names.push_back(sym->name());
            }
        }
        if(names.size()>0) {
            text_.add_gutter() << "value_type " << *(names.begin());
            for(auto it=names.begin()+1; it!=names.end(); ++it) {
                text_ << ", " << *it;
            }
            text_.end_line(";");
        }
    }

    // ------------- statements ------------- //
    for(auto& stmt : e->statements()) {
        if(stmt->is_local_declaration()) continue;

        // these all must be handled
        text_.add_gutter();
        stmt->accept(this);
        if (not stmt->is_if()) {
            text_.end_line(";");
        }
    }
}

void CPrinter::visit(IfExpression *e) {
    // for now we remove the brackets around the condition because
    // the binary expression printer adds them, and we want to work
    // around the -Wparentheses-equality warning
    text_ << "if(";
    e->condition()->accept(this);
    text_ << ") {\n";
    increase_indentation();
    e->true_branch()->accept(this);
    decrease_indentation();
    text_.add_line("}");
    // check if there is a false-branch, i.e. if
    // there is an "else" branch to print
    if (auto fb = e->false_branch()) {
        text_.add_gutter() << "else ";
        // use recursion for "else if"
        if (fb->is_if()) {
            fb->accept(this);
        }
        // otherwise print the "else" block
        else {
            text_ << "{\n";
            increase_indentation();
            fb->accept(this);
            decrease_indentation();
            text_.add_line("}");
        }
    }
}

// NOTE: net_receive() is classified as a ProcedureExpression
void CPrinter::visit(ProcedureExpression *e) {
    // print prototype
    text_.add_gutter() << "void " << e->name() << "(int i_";
    for(auto& arg : e->args()) {
        text_ << ", value_type " << arg->is_argument()->name();
    }
    if(e->kind() == procedureKind::net_receive) {
        text_.end_line(") override {");
    }
    else {
        text_.end_line(") {");
    }

    if(!e->scope()) { // error: semantic analysis has not been performed
        throw compiler_exception(
            "CPrinter attempt to print Procedure " + e->name()
            + " for which semantic analysis has not been performed",
            e->location());
    }

    // print body
    increase_indentation();
    e->body()->accept(this);

    // close the function body
    decrease_indentation();
    text_.add_line("}");
    text_.add_line();
}

void CPrinter::visit(APIMethod *e) {
    // print prototype
    text_.add_gutter() << "void " << e->name() << "() override {";
    text_.end_line();

    if(!e->scope()) { // error: semantic analysis has not been performed
        throw compiler_exception(
            "CPrinter attempt to print APIMethod " + e->name()
            + " for which semantic analysis has not been performed",
            e->location());
    }

    // only print the body if it has contents
    if(e->is_api_method()->body()->statements().size()) {
        increase_indentation();

        // create local indexed views
        for(auto &symbol : e->scope()->locals()) {
            auto var = symbol.second->is_local_variable();
            if (!var->is_indexed()) continue;

            auto external = var->external_variable();
            auto const& name = var->name();
            auto const& index_name = external->index_name();

            text_.add_gutter();
            text_ << "auto " + index_name + " = ";

            if(external->is_cell_indexed_variable()) {
                text_ << "util::indirect_view(util::indirect_view(" + index_name + "_, vec_ci_), node_index_);\n";
            }
            else if(external->is_ion()) {
                auto channel = external->ion_channel();
                auto iname = ion_store(channel);
                text_ << "util::indirect_view(" << iname << "." << name << ", " << ion_store(channel) << ".index);\n";
            }
            else {
                text_ << "util::indirect_view(" + index_name + "_, node_index_);\n";
            }
        }

        // get loop dimensions
        text_.add_line("int n_ = node_index_.size();");

        // hand off printing of loops to optimized or unoptimized backend
        if(optimize_) {
            print_APIMethod_optimized(e);
        }
        else {
            print_APIMethod_unoptimized(e);
        }
    }

    // close up the loop body
    text_.add_line("}");
    text_.add_line();
}

void CPrinter::emit_api_loop(APIMethod* e,
                             const std::string& start,
                             const std::string& end,
                             const std::string& inc) {
    text_.add_gutter();
    text_ << "for (" << start << "; " << end << "; " << inc << ") {";
    text_.end_line();
    text_.increase_indentation();

    // loads from external indexed arrays
    for(auto &symbol : e->scope()->locals()) {
        auto var = symbol.second->is_local_variable();
        if(is_input(var)) {
            auto ext = var->external_variable();
            text_.add_gutter() << "value_type ";
            var->accept(this);
            text_ << " = ";
            ext->accept(this);
            text_.end_line(";");
        }
    }

    // print the body of the loop
    e->body()->accept(this);

    // perform update of external variables (currents etc)
    for(auto &symbol : e->scope()->locals()) {
        auto var = symbol.second->is_local_variable();
        if(is_output(var)) {
            auto ext = var->external_variable();
            text_.add_gutter();
            ext->accept(this);
            text_ << (ext->op() == tok::plus ? " += " : " -= ");
            var->accept(this);
            text_.end_line(";");
        }
    }

    text_.decrease_indentation();
    text_.add_line("}");
}

void CPrinter::print_APIMethod_unoptimized(APIMethod* e) {
    emit_api_loop(e, "int i_ = 0", "i_ < n_", "++i_");
    decrease_indentation();

    return;
}

void CPrinter::print_APIMethod_optimized(APIMethod* e) {
    // ------------- get mechanism properties ------------- //

    // make a list of all the local variables that have to be
    // written out to global memory via an index
    auto is_aliased = [this] (Symbol* s) -> LocalVariable* {
        if(is_output(s)) {
            return s->is_local_variable();
        }
        return nullptr;
    };

    std::vector<LocalVariable*> aliased_variables;
    if(is_point_process()) {
        for(auto &l : e->scope()->locals()) {
            if(auto var = is_aliased(l.second.get())) {
                aliased_variables.push_back(var);
            }
        }
    }

    aliased_output_ = aliased_variables.size()>0;

    // only proceed with optimized output if the ouputs are aliased
    // because all optimizations are for using ghost buffers to avoid
    // race conditions in vectorized code
    if(!aliased_output_) {
        print_APIMethod_unoptimized(e);
        return;
    }

    // ------------- block loop ------------- //

    text_.add_line("constexpr int BSIZE = 4;");
    text_.add_line("int NB = n_/BSIZE;");
    for(auto out: aliased_variables) {
        text_.add_line("value_type " + out->name() +  "[BSIZE];");
    }

    text_.add_line("for(int b_=0; b_<NB; ++b_) {");
    text_.increase_indentation();
    text_.add_line("int BSTART = BSIZE*b_;");
    text_.add_line("int i_ = BSTART;");


    text_.add_line("for(int j_=0; j_<BSIZE; ++j_, ++i_) {");
    text_.increase_indentation();

    // loads from external indexed arrays
    for(auto &symbol : e->scope()->locals()) {
        auto var = symbol.second->is_local_variable();
        if(is_input(var)) {
            auto ext = var->external_variable();
            text_.add_gutter() << "value_type ";
            var->accept(this);
            text_ << " = ";
            ext->accept(this);
            text_.end_line(";");
        }
    }

    e->body()->accept(this);

    text_.decrease_indentation();
    text_.add_line("}"); // end inner compute loop

    text_.add_line("i_ = BSTART;");
    text_.add_line("for(int j_=0; j_<BSIZE; ++j_, ++i_) {");
    text_.increase_indentation();

    for(auto out: aliased_variables) {
        text_.add_gutter();
        auto ext = out->external_variable();
        ext->accept(this);
        text_ << (ext->op() == tok::plus ? " += " : " -= ");
        out->accept(this);
        text_.end_line(";");
    }

    text_.decrease_indentation();
    text_.add_line("}"); // end inner write loop
    text_.decrease_indentation();
    text_.add_line("}"); // end outer block loop

    // ------------- block tail loop ------------- //

    text_.add_line("int j_ = 0;");
    text_.add_line("for(int i_=NB*BSIZE; i_<n_; ++j_, ++i_) {");
    text_.increase_indentation();

    for(auto &symbol : e->scope()->locals()) {
        auto var = symbol.second->is_local_variable();
        if(is_input(var)) {
            auto ext = var->external_variable();
            text_.add_gutter() << "value_type ";
            var->accept(this);
            text_ << " = ";
            ext->accept(this);
            text_.end_line(";");
        }
    }

    e->body()->accept(this);

    text_.decrease_indentation();
    text_.add_line("}"); // end inner compute loop
    text_.add_line("j_ = 0;");
    text_.add_line("for(int i_=NB*BSIZE; i_<n_; ++j_, ++i_) {");
    text_.increase_indentation();

    for(auto out: aliased_variables) {
        text_.add_gutter();
        auto ext = out->external_variable();
        ext->accept(this);
        text_ << (ext->op() == tok::plus ? " += " : " -= ");
        out->accept(this);
        text_.end_line(";");
    }

    text_.decrease_indentation();
    text_.add_line("}"); // end block tail loop

    decrease_indentation();

    aliased_output_ = false;
}

void CPrinter::visit(CallExpression *e) {
    text_ << e->name() << "(i_";
    for(auto& arg: e->args()) {
        text_ << ", ";
        arg->accept(this);
    }
    text_ << ")";
}

void CPrinter::visit(AssignmentExpression *e) {
    e->lhs()->accept(this);
    text_ << " = ";
    e->rhs()->accept(this);
}

void CPrinter::visit(PowBinaryExpression *e) {
    text_ << "std::pow(";
    e->lhs()->accept(this);
    text_ << ", ";
    e->rhs()->accept(this);
    text_ << ")";
}

void CPrinter::visit(BinaryExpression *e) {
    auto pop = parent_op_;
    // TODO unit tests for parenthesis and binops
    bool use_brackets =
        Lexer::binop_precedence(pop) > Lexer::binop_precedence(e->op())
        || (pop==tok::divide && e->op()==tok::times);
    parent_op_ = e->op();

    auto lhs = e->lhs();
    auto rhs = e->rhs();
    if(use_brackets) {
        text_ << "(";
    }
    lhs->accept(this);
    switch(e->op()) {
        case tok::minus :
            text_ << "-";
            break;
        case tok::plus :
            text_ << "+";
            break;
        case tok::times :
            text_ << "*";
            break;
        case tok::divide :
            text_ << "/";
            break;
        case tok::lt     :
            text_ << "<";
            break;
        case tok::lte    :
            text_ << "<=";
            break;
        case tok::gt     :
            text_ << ">";
            break;
        case tok::gte    :
            text_ << ">=";
            break;
        case tok::equality :
            text_ << "==";
            break;
        default :
            throw compiler_exception(
                "CPrinter unsupported binary operator " + yellow(token_string(e->op())),
                e->location());
    }
    rhs->accept(this);
    if(use_brackets) {
        text_ << ")";
    }

    // reset parent precedence
    parent_op_ = pop;
}
