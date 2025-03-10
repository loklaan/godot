/*************************************************************************/
/*  debug_adapter_parser.cpp                                             */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2021 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2021 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#include "debug_adapter_parser.h"

#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/script_editor_debugger.h"
#include "editor/editor_node.h"

void DebugAdapterParser::_bind_methods() {
	// Requests
	ClassDB::bind_method(D_METHOD("req_initialize", "params"), &DebugAdapterParser::req_initialize);
	ClassDB::bind_method(D_METHOD("req_disconnect", "params"), &DebugAdapterParser::prepare_success_response);
	ClassDB::bind_method(D_METHOD("req_launch", "params"), &DebugAdapterParser::req_launch);
	ClassDB::bind_method(D_METHOD("req_terminate", "params"), &DebugAdapterParser::req_terminate);
	ClassDB::bind_method(D_METHOD("req_configurationDone", "params"), &DebugAdapterParser::prepare_success_response);
	ClassDB::bind_method(D_METHOD("req_pause", "params"), &DebugAdapterParser::req_pause);
	ClassDB::bind_method(D_METHOD("req_continue", "params"), &DebugAdapterParser::req_continue);
	ClassDB::bind_method(D_METHOD("req_threads", "params"), &DebugAdapterParser::req_threads);
	ClassDB::bind_method(D_METHOD("req_stackTrace", "params"), &DebugAdapterParser::req_stackTrace);
	ClassDB::bind_method(D_METHOD("req_setBreakpoints", "params"), &DebugAdapterParser::req_setBreakpoints);
	ClassDB::bind_method(D_METHOD("req_scopes", "params"), &DebugAdapterParser::req_scopes);
	ClassDB::bind_method(D_METHOD("req_variables", "params"), &DebugAdapterParser::req_variables);
	ClassDB::bind_method(D_METHOD("req_next", "params"), &DebugAdapterParser::req_next);
	ClassDB::bind_method(D_METHOD("req_stepIn", "params"), &DebugAdapterParser::req_stepIn);
}

Dictionary DebugAdapterParser::prepare_base_event() const {
	Dictionary event;
	event["type"] = "event";

	return event;
}

Dictionary DebugAdapterParser::prepare_success_response(const Dictionary &p_params) const {
	Dictionary response;
	response["type"] = "response";
	response["request_seq"] = p_params["seq"];
	response["command"] = p_params["command"];
	response["success"] = true;

	return response;
}

Dictionary DebugAdapterParser::prepare_error_response(const Dictionary &p_params, DAP::ErrorType err_type, const Dictionary &variables) const {
	Dictionary response, body;
	response["type"] = "response";
	response["request_seq"] = p_params["seq"];
	response["command"] = p_params["command"];
	response["success"] = false;
	response["body"] = body;

	DAP::Message message;
	String error, error_desc;
	switch (err_type) {
		case DAP::ErrorType::UNKNOWN:
			error = "unknown";
			error_desc = "An unknown error has ocurred when processing the request.";
			break;
		case DAP::ErrorType::WRONG_PATH:
			error = "wrong_path";
			error_desc = "The editor and client are working on different paths; the client is on \"{clientPath}\", but the editor is on \"{editorPath}\"";
	}

	message.id = err_type;
	message.format = error_desc;
	message.variables = variables;
	response["message"] = error;
	body["error"] = message.to_json();

	return response;
}

Dictionary DebugAdapterParser::req_initialize(const Dictionary &p_params) const {
	Dictionary response = prepare_success_response(p_params);
	Dictionary args = p_params["arguments"];

	Ref<DAPeer> peer = DebugAdapterProtocol::get_singleton()->get_current_peer();

	peer->linesStartAt1 = args.get("linesStartAt1", false);
	peer->columnsStartAt1 = args.get("columnsStartAt1", false);
	peer->supportsVariableType = args.get("supportsVariableType", false);
	peer->supportsInvalidatedEvent = args.get("supportsInvalidatedEvent", false);

	DAP::Capabilities caps;
	response["body"] = caps.to_json();

	DebugAdapterProtocol::get_singleton()->notify_initialized();

	return response;
}

Dictionary DebugAdapterParser::req_launch(const Dictionary &p_params) {
	Dictionary args = p_params["arguments"];
	if (args.has("project") && !is_valid_path(args["project"])) {
		Dictionary variables;
		variables["clientPath"] = args["project"];
		variables["editorPath"] = ProjectSettings::get_singleton()->get_resource_path();
		return prepare_error_response(p_params, DAP::ErrorType::WRONG_PATH, variables);
	}

	ScriptEditorDebugger *dbg = EditorDebuggerNode::get_singleton()->get_default_debugger();
	if ((bool)args["noDebug"] != dbg->is_skip_breakpoints()) {
		dbg->debug_skip_breakpoints();
	}

	EditorNode::get_singleton()->run_play();
	DebugAdapterProtocol::get_singleton()->notify_process();

	return prepare_success_response(p_params);
}

Dictionary DebugAdapterParser::req_terminate(const Dictionary &p_params) const {
	EditorNode::get_singleton()->run_stop();

	return prepare_success_response(p_params);
}

Dictionary DebugAdapterParser::req_pause(const Dictionary &p_params) const {
	EditorNode::get_singleton()->get_pause_button()->set_pressed(true);
	EditorDebuggerNode::get_singleton()->_paused();

	DebugAdapterProtocol::get_singleton()->notify_stopped_paused();

	return prepare_success_response(p_params);
}

Dictionary DebugAdapterParser::req_continue(const Dictionary &p_params) const {
	EditorNode::get_singleton()->get_pause_button()->set_pressed(false);
	EditorDebuggerNode::get_singleton()->_paused();

	DebugAdapterProtocol::get_singleton()->notify_continued();

	return prepare_success_response(p_params);
}

Dictionary DebugAdapterParser::req_threads(const Dictionary &p_params) const {
	Dictionary response = prepare_success_response(p_params), body;
	response["body"] = body;

	Array arr;
	DAP::Thread thread;

	thread.id = 1; // Hardcoded because Godot only supports debugging one thread at the moment
	thread.name = "Main";
	arr.push_back(thread.to_json());
	body["threads"] = arr;

	return response;
}

Dictionary DebugAdapterParser::req_stackTrace(const Dictionary &p_params) const {
	if (DebugAdapterProtocol::get_singleton()->_processing_stackdump) {
		return Dictionary();
	}

	Dictionary response = prepare_success_response(p_params), body;
	response["body"] = body;

	bool lines_at_one = DebugAdapterProtocol::get_singleton()->get_current_peer()->linesStartAt1;
	bool columns_at_one = DebugAdapterProtocol::get_singleton()->get_current_peer()->columnsStartAt1;

	Array arr;
	DebugAdapterProtocol *dap = DebugAdapterProtocol::get_singleton();
	for (Map<DAP::StackFrame, List<int>>::Element *E = dap->stackframe_list.front(); E; E = E->next()) {
		DAP::StackFrame sf = E->key();
		if (!lines_at_one) {
			sf.line--;
		}
		if (!columns_at_one) {
			sf.column--;
		}

		arr.push_back(sf.to_json());
	}

	body["stackFrames"] = arr;
	return response;
}

Dictionary DebugAdapterParser::req_setBreakpoints(const Dictionary &p_params) {
	Dictionary response = prepare_success_response(p_params), body;
	response["body"] = body;

	Dictionary args = p_params["arguments"];
	DAP::Source source;
	source.from_json(args["source"]);

	bool lines_at_one = DebugAdapterProtocol::get_singleton()->get_current_peer()->linesStartAt1;

	if (!is_valid_path(source.path)) {
		Dictionary variables;
		variables["clientPath"] = source.path;
		variables["editorPath"] = ProjectSettings::get_singleton()->get_resource_path();
		return prepare_error_response(p_params, DAP::ErrorType::WRONG_PATH, variables);
	}

	Array breakpoints = args["breakpoints"], lines;
	for (int i = 0; i < breakpoints.size(); i++) {
		DAP::SourceBreakpoint breakpoint;
		breakpoint.from_json(breakpoints[i]);

		lines.push_back(breakpoint.line + !lines_at_one);
	}

	EditorDebuggerNode::get_singleton()->set_breakpoints(ProjectSettings::get_singleton()->localize_path(source.path), lines);
	Array updated_breakpoints = DebugAdapterProtocol::get_singleton()->update_breakpoints(source.path, lines);
	body["breakpoints"] = updated_breakpoints;

	return response;
}

Dictionary DebugAdapterParser::req_scopes(const Dictionary &p_params) {
	Dictionary response = prepare_success_response(p_params), body;
	response["body"] = body;

	Dictionary args = p_params["arguments"];
	int frame_id = args["frameId"];
	Array scope_list;

	DAP::StackFrame frame;
	frame.id = frame_id;
	Map<DAP::StackFrame, List<int>>::Element *E = DebugAdapterProtocol::get_singleton()->stackframe_list.find(frame);
	if (E) {
		ERR_FAIL_COND_V(E->value().size() != 3, prepare_error_response(p_params, DAP::ErrorType::UNKNOWN));
		for (int i = 0; i < 3; i++) {
			DAP::Scope scope;
			scope.variablesReference = E->value()[i];
			switch (i) {
				case 0:
					scope.name = "Locals";
					scope.presentationHint = "locals";
					break;
				case 1:
					scope.name = "Members";
					scope.presentationHint = "members";
					break;
				case 2:
					scope.name = "Globals";
					scope.presentationHint = "globals";
			}

			scope_list.push_back(scope.to_json());
		}
	}

	EditorDebuggerNode::get_singleton()->get_default_debugger()->request_stack_dump(frame_id);
	DebugAdapterProtocol::get_singleton()->_current_frame = frame_id;

	body["scopes"] = scope_list;
	return response;
}

Dictionary DebugAdapterParser::req_variables(const Dictionary &p_params) const {
	// If _remaining_vars > 0, the debugee is still sending a stack dump to the editor.
	if (DebugAdapterProtocol::get_singleton()->_remaining_vars > 0) {
		return Dictionary();
	}

	Dictionary response = prepare_success_response(p_params), body;
	response["body"] = body;

	Dictionary args = p_params["arguments"];
	int variable_id = args["variablesReference"];

	Map<int, Array>::Element *E = DebugAdapterProtocol::get_singleton()->variable_list.find(variable_id);
	if (E) {
		body["variables"] = E ? E->value() : Array();
		return response;
	} else {
		return Dictionary();
	}
}

Dictionary DebugAdapterParser::req_next(const Dictionary &p_params) const {
	EditorDebuggerNode::get_singleton()->get_default_debugger()->debug_next();
	DebugAdapterProtocol::get_singleton()->_stepping = true;

	return prepare_success_response(p_params);
}

Dictionary DebugAdapterParser::req_stepIn(const Dictionary &p_params) const {
	EditorDebuggerNode::get_singleton()->get_default_debugger()->debug_step();
	DebugAdapterProtocol::get_singleton()->_stepping = true;

	return prepare_success_response(p_params);
}

Dictionary DebugAdapterParser::ev_initialized() const {
	Dictionary event = prepare_base_event();
	event["event"] = "initialized";

	return event;
}

Dictionary DebugAdapterParser::ev_process(const String &p_command) const {
	Dictionary event = prepare_base_event(), body;
	event["event"] = "process";
	event["body"] = body;

	body["name"] = OS::get_singleton()->get_executable_path();
	body["startMethod"] = p_command;

	return event;
}

Dictionary DebugAdapterParser::ev_terminated() const {
	Dictionary event = prepare_base_event();
	event["event"] = "terminated";

	return event;
}

Dictionary DebugAdapterParser::ev_exited(const int &p_exitcode) const {
	Dictionary event = prepare_base_event(), body;
	event["event"] = "exited";
	event["body"] = body;

	body["exitCode"] = p_exitcode;

	return event;
}

Dictionary DebugAdapterParser::ev_stopped() const {
	Dictionary event = prepare_base_event(), body;
	event["event"] = "stopped";
	event["body"] = body;

	body["threadId"] = 1;

	return event;
}

Dictionary DebugAdapterParser::ev_stopped_paused() const {
	Dictionary event = ev_stopped();
	Dictionary body = event["body"];

	body["reason"] = "paused";
	body["description"] = "Paused";

	return event;
}

Dictionary DebugAdapterParser::ev_stopped_exception(const String &p_error) const {
	Dictionary event = ev_stopped();
	Dictionary body = event["body"];

	body["reason"] = "exception";
	body["description"] = "Exception";
	body["text"] = p_error;

	return event;
}

Dictionary DebugAdapterParser::ev_stopped_breakpoint(const int &p_id) const {
	Dictionary event = ev_stopped();
	Dictionary body = event["body"];

	body["reason"] = "breakpoint";
	body["description"] = "Breakpoint";

	Array breakpoints;
	breakpoints.push_back(p_id);
	body["hitBreakpointIds"] = breakpoints;

	return event;
}

Dictionary DebugAdapterParser::ev_stopped_step() const {
	Dictionary event = ev_stopped();
	Dictionary body = event["body"];

	body["reason"] = "step";
	body["description"] = "Breakpoint";

	return event;
}

Dictionary DebugAdapterParser::ev_continued() const {
	Dictionary event = prepare_base_event(), body;
	event["event"] = "continued";
	event["body"] = body;

	body["threadId"] = 1;

	return event;
}

Dictionary DebugAdapterParser::ev_output(const String &p_message) const {
	Dictionary event = prepare_base_event(), body;
	event["event"] = "output";
	event["body"] = body;

	body["category"] = "stdout";
	body["output"] = p_message + "\r\n";

	return event;
}
