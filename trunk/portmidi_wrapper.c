#include "ruby.h"
#include "portmidi.h"
#include "portmidi_wrapper.h"

/*
 MidiDevice constructor. Opens the Device with the given device id
 Will open an Input stream on an input device and an Output stream on
 an output device.
*/
static VALUE md_init(VALUE self, VALUE device_number) {
	DeviceStream *stream;
	PmError error;
	int device_id;
	int count;
	PmStream *midiStream;
	
	
	device_id = NUM2INT(device_number);
	
  	Data_Get_Struct(self,DeviceStream,stream);

	const PmDeviceInfo *deviceInfo = Pm_GetDeviceInfo(device_id);
	if(deviceInfo->input) {
		error = Pm_OpenInput(&midiStream, (PmDeviceID)device_id, NULL, 255, NULL, NULL);
		stream->type = 0;
	} else {
		error = Pm_OpenOutput(&midiStream, (PmDeviceID)device_id, NULL, 255, NULL, NULL, 0);
		stream->type = 1;
	}
	stream->stream = midiStream;
  	return self; 
}

static VALUE md_free(void *p) {
	DeviceStream *stream;
	stream = p;
	Pm_Close(stream->stream);
	free(stream);
}

static VALUE md_alloc(VALUE klass) {
	DeviceStream *stream;
	VALUE obj;
	stream = malloc(sizeof(*stream));
	stream->stream = NULL;
	obj = Data_Wrap_Struct(klass, 0, md_free, stream);
	return obj;	
}
/*
	Reads an Event from the Input stream. An event consists of up to 4 bytes. The method returns 
	error,[b1,b2,b3,b4] where error is the number of bytes read or the error code (if <0)
	call-seq:
		read -> error, [b1,b2,b3,b4]
		
*/ 
static VALUE md_read(VALUE self) {
	DeviceStream *stream;
	VALUE msg;
	VALUE err;
	VALUE array;
	VALUE msg_ary;
	int data = 0;
	int shift = 0;
	VALUE msg_data;
	PmEvent message;
	PmError error;

	Data_Get_Struct(self,DeviceStream,stream);
	error = Pm_Read(stream->stream, &message, 1);
	
	array = rb_ary_new2(2);
	err = INT2NUM(error);
	rb_ary_push(array, err);

	if (error>0) {
		msg_ary = rb_ary_new2(4);
		for (shift = 0; shift < 32; shift += 8) {
			data = (message.message >> shift) & 0xFF;
			msg_data = INT2NUM(data);
			rb_ary_push(msg_ary, msg_data);
		}
		rb_ary_push(array, msg_ary);
	}
	return array;
	
}
/*
Writes sysex message. The message must be a properly terminated sysex message,
otherwise very bad things may happen. returns 0 on success and error code otherwise

call-seq:
 	write_sysex(message) -> error

*/
static VALUE md_write_sysex(VALUE self, VALUE sysex) {
	DeviceStream *stream;
	PmError error;
	VALUE err;
	VALUE sysex_string;
	Data_Get_Struct(self,DeviceStream,stream);
	sysex_string = StringValue(sysex);
	
	error = Pm_WriteSysEx(stream->stream, 0, (unsigned char *)RSTRING(sysex_string)->ptr);	
	err = INT2NUM(error);
	return err;	
	
}
/* 
writes a short midi message (such as note on, note off etc.). returns 0 on succes, error code otherwise
message is an array of bytes (as Fixnums) that is between 2 and 4 bytes long. The bytes are and'ed with 0xff 
before sending.

call-seq:
	write_short(message) -> error
	
*/
static VALUE md_write_short(VALUE self, VALUE bytes) {
	DeviceStream *stream;
	PmError error;
	VALUE err;
	int shift = 32;
	int i = 0;
	long msg = 0;
	VALUE byte_value;
	long byte = 0;
	long len;
	
	Data_Get_Struct(self,DeviceStream,stream);
	
	len = RARRAY(bytes)->len;
	if (len > 4) len = 4;
	
	for (i=0;i<len;i++) {
		byte_value = rb_ary_entry(bytes,i);
		byte = NUM2LONG(byte_value);
		msg = msg | (byte & 0xFF) << (i)*8;
		shift -= 8;

	}
	error = Pm_WriteShort(stream->stream, 0, msg);
	err = INT2NUM(error);
	return err;
}
/*
  Returns an error message in textual form for a given error code

call-seq:
	error_text(error_code) -> message

*/
static VALUE md_error_text(VALUE self, VALUE error_value) {
	const char *error_message;
	PmError error;
	VALUE error_text;
	
	error = NUM2INT(error_value);
	error_message = Pm_GetErrorText(error);
	error_text = rb_str_new2(error_message);
	return error_text;
}
/*
	tests for host error
	can be called at any time or after a method returns the host error error code
	if returns true, host_error_text should be called to clear the error

call-seq:
	host_error? -> boolean

*/

static VALUE md_host_error(VALUE self) {
	DeviceStream *stream;
	int error;
	
	Data_Get_Struct(self,DeviceStream,stream);
	error = Pm_HasHostError(stream->stream);
	if(error) return Qtrue;
	return Qfalse;
}

/*

	returns an error message if a host error occured, returns an empty string otherwise
	TODO: eventually unify with host_error?

call-seq:
	host_error_text -> message
	
*/
static VALUE md_host_error_text(VALUE self) {
	VALUE error_text;
	char error_message[PM_HOST_ERROR_MSG_LEN];
	
	Pm_GetHostErrorText(error_message, PM_HOST_ERROR_MSG_LEN);
	error_text = rb_str_new2(error_message);
	return error_text;
}

/*
  Returns true if the input stream contains events to be fetched by read. 
  
  Returns false if no events are pending
  
  Returns error code (<0) if an error occurred

  call-seq:
	md_poll -> result
	
*/
static VALUE md_poll(VALUE self) {
	DeviceStream *stream;
	VALUE more;
	PmError error;
	Data_Get_Struct(self,DeviceStream,stream);
	error = Pm_Poll(stream->stream);
	if (error == TRUE) return Qtrue;
	if (error == FALSE) return Qfalse;
	more = INT2NUM(error);
	return more;
}

/*
Returns the number of available midi devices

call-seq:
  count -> num

*/
static VALUE mdd_count(VALUE self) {
	int count;
	VALUE count_num;
	count = Pm_CountDevices();
	count_num = INT2NUM(count);
	return count_num;
}

/*
Returns a DeviceInfo object

call-seq:
	get -> DeviceInfo

*/
static VALUE mdd_get(VALUE self, VALUE device_number) {
  PmDeviceInfo* deviceInfo;
  VALUE obj;
  int device_id = 0;
  int device_count = 0;

  device_count = Pm_CountDevices();

  device_id = NUM2INT(device_number);

  if (device_id > device_count -1) return Qnil;
  
  deviceInfo = Pm_GetDeviceInfo(device_id);
  obj = Data_Wrap_Struct(cMidiDeviceInfo, 0, 0, deviceInfo);
  return obj;
}
/*
Returns the device name

call-seq:
	name -> name

*/
static VALUE mdd_name(VALUE self) {
  VALUE string;
  PmDeviceInfo* deviceInfo;
  Data_Get_Struct(self,PmDeviceInfo, deviceInfo);
  string = rb_str_new2(deviceInfo->name);
  return string;
}

/*
  returns true if device is an input device

call-seq:
	input? -> boolean

*/

static VALUE mdd_input(VALUE self) {
	VALUE ret;
  	PmDeviceInfo* deviceInfo;
  	Data_Get_Struct(self,PmDeviceInfo, deviceInfo);
	if (deviceInfo->input) return Qtrue;
	return Qfalse;
}

/*
  returns true if device is an out device

call-seq:
	output? -> boolean

*/
static VALUE mdd_output(VALUE self) {
	VALUE ret;
  	PmDeviceInfo* deviceInfo;
  	Data_Get_Struct(self,PmDeviceInfo, deviceInfo);
	if (deviceInfo->output) return Qtrue;
	return Qfalse;
}

/*
  initializes the midi system

  fires Pm_Initialize()

*/

static VALUE ms_init(VALUE self) {
	return self;
	Pm_Initialize();
}

/*
  this function should be called after the midi system has been used. Fires Pm_Terminate()

*/
static VALUE ms_destroy(VALUE self) {
	return self;
	Pm_Terminate();
}


void Init_portmidi() {
	mPortmidi = rb_define_module("Portmidi");

	// MidiSystem
	cMidiSystem = rb_define_class_under(mPortmidi, "MidiSystem", rb_cObject);
	rb_define_method(cMidiSystem, "initialize", ms_init, 0);
	rb_define_method(cMidiSystem, "destroy", ms_destroy, 0);

	// MidiDeviceInfo
	cMidiDeviceInfo = rb_define_class_under(mPortmidi,"MidiDeviceInfo", rb_cObject);
	rb_define_singleton_method(cMidiDeviceInfo, "get", mdd_get, 1);
	rb_define_singleton_method(cMidiDeviceInfo, "count", mdd_count, 0);
	rb_define_method(cMidiDeviceInfo, "name", mdd_name, 0);
	rb_define_method(cMidiDeviceInfo, "input?", mdd_input, 0);
	rb_define_method(cMidiDeviceInfo, "output?", mdd_output, 0);

	// MidiDevice
	cMidiDevice = rb_define_class_under(mPortmidi, "MidiDevice", rb_cObject);
	rb_define_alloc_func(cMidiDevice, md_alloc);
  	rb_define_method(cMidiDevice, "initialize", md_init, 1);
  	rb_define_method(cMidiDevice, "poll", md_poll, 0);
  	rb_define_method(cMidiDevice, "read", md_read, 0);
 	rb_define_method(cMidiDevice, "write_short", md_write_short, 1);
	rb_define_method(cMidiDevice, "write_sysex", md_write_sysex, 1);
	rb_define_method(cMidiDevice, "error_text", md_error_text, 1);
	rb_define_method(cMidiDevice, "host_error?", md_host_error, 0);
	rb_define_method(cMidiDevice, "host_error_text", md_host_error_text, 0);
	
	

}