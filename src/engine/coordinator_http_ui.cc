// The Firmament project
// Copyright (c) 2011-2012 Malte Schwarzkopf <malte.schwarzkopf@cl.cam.ac.uk>
//

#include "engine/coordinator_http_ui.h"

#include <string>
#include <vector>

#include <boost/uuid/uuid_io.hpp>
#include <boost/bind.hpp>
#include <google/protobuf/text_format.h>
#include <pb2json.h>

#include "base/job_desc.pb.h"
#include "engine/coordinator.h"
#include "misc/utils.h"

namespace firmament {
namespace webui {

using pion::net::HTTPResponseWriter;
using pion::net::HTTPResponseWriterPtr;
using pion::net::HTTPResponse;
using pion::net::HTTPTypes;
using pion::net::HTTPServer;
using pion::net::TCPConnection;

using ctemplate::TemplateDictionary;

CoordinatorHTTPUI::CoordinatorHTTPUI(shared_ptr<Coordinator> coordinator)
  : coordinator_(coordinator),
    active_(true) { }

CoordinatorHTTPUI::~CoordinatorHTTPUI() {
  // Kill the server without waiting for connections to terminate
  if (coordinator_http_server_->isListening()) {
    coordinator_http_server_->stop(false);
    coordinator_http_server_->join();
    LOG(INFO) << "Coordinator HTTP UI server stopped.";
  }
  LOG(INFO) << "Coordinator HTTP UI server destroyed.";
}

void CoordinatorHTTPUI::AddHeaderToTemplate(TemplateDictionary* dict,
                                            ResourceID_t uuid,
                                            ErrorMessage_t* err) {
  // HTML header
  TemplateDictionary* header_sub_dict = dict->AddIncludeDictionary("HEADER");
  header_sub_dict->SetFilename("src/webui/header.tpl");
  // Page header
  TemplateDictionary* pgheader_sub_dict =
      dict->AddIncludeDictionary("PAGE_HEADER");
  pgheader_sub_dict->SetFilename("src/webui/page_header.tpl");
  pgheader_sub_dict->SetValue("RESOURCE_ID", to_string(uuid));
  // Error message, if set
  if (err) {
    TemplateDictionary* err_dict =
        pgheader_sub_dict->AddSectionDictionary("ERR");
    err_dict->SetValue("ERR_TITLE", err->first);
    err_dict->SetValue("ERR_TEXT", err->second);
  }
}

void CoordinatorHTTPUI::AddFooterToTemplate(TemplateDictionary* dict) {
  // Page footer
  TemplateDictionary* pgheader_sub_dict =
      dict->AddIncludeDictionary("PAGE_FOOTER");
  pgheader_sub_dict->SetFilename("src/webui/page_footer.tpl");
}

void CoordinatorHTTPUI::HandleJobSubmitURI(HTTPRequestPtr& http_request,  // NOLINT
                                           TCPConnectionPtr& tcp_conn) {  // NOLINT
  LogRequest(http_request);
  // Check if we have a JobDescriptor as part of the POST parameters
  HTTPTypes::QueryParams& params = http_request->getQueryParams();
  string* job_descriptor_param = FindOrNull(params, "test");
  if (http_request->getMethod() != "POST" || !job_descriptor_param) {
    ErrorResponse(HTTPTypes::RESPONSE_CODE_SERVER_ERROR, http_request,
                  tcp_conn);
    return;
  }
  // We're okay to continue
  HTTPResponseWriterPtr writer = InitOkResponse(http_request, tcp_conn);
  // Submit the JD to the coordinator
  JobDescriptor job_descriptor;
  google::protobuf::TextFormat::ParseFromString(*job_descriptor_param,
                                                &job_descriptor);
  VLOG(3) << "JD:" << job_descriptor.DebugString();
  string job_id = coordinator_->SubmitJob(job_descriptor);
  // Return the job ID to the client
  writer->write(job_id);
  FinishOkResponse(writer);
}

void CoordinatorHTTPUI::HandleRootURI(HTTPRequestPtr& http_request,  // NOLINT
                                      TCPConnectionPtr& tcp_conn) {  // NOLINT
  LogRequest(http_request);
  HTTPResponseWriterPtr writer = InitOkResponse(http_request, tcp_conn);
  // Individual to this request
  //HTTPTypes::QueryParams &params = http_request->getQueryParams();
  TemplateDictionary dict("main");
  AddHeaderToTemplate(&dict, coordinator_->uuid(), NULL);
  dict.SetValue("COORD_ID", to_string(coordinator_->uuid()));
  dict.SetIntValue("NUM_JOBS_KNOWN", coordinator_->NumJobs());
  dict.SetIntValue("NUM_JOBS_RUNNING", coordinator_->NumJobsInState(
      JobDescriptor::RUNNING));
  dict.SetIntValue("NUM_TASKS_KNOWN", coordinator_->NumTasks());
  dict.SetIntValue("NUM_TASKS_RUNNING", coordinator_->NumTasksInState(
      TaskDescriptor::RUNNING));
  // The +1 is because the coordinator itself is a resource, too.
  dict.SetIntValue("NUM_RESOURCES_KNOWN", coordinator_->NumResources() + 1);
  dict.SetIntValue("NUM_RESOURCES_LOCAL", coordinator_->NumResources());
  AddFooterToTemplate(&dict);
  string output;
  ExpandTemplate("src/webui/main.tpl", ctemplate::DO_NOT_STRIP, &dict, &output);
  writer->write(output);
  FinishOkResponse(writer);
}

void CoordinatorHTTPUI::HandleFaviconURI(HTTPRequestPtr& http_request,  // NOLINT
                                         TCPConnectionPtr& tcp_conn) {  // NOLINT
  LogRequest(http_request);
  HTTPResponseWriterPtr writer = InitOkResponse(http_request, tcp_conn);
  ErrorResponse(HTTPTypes::RESPONSE_CODE_NOT_FOUND, http_request, tcp_conn);
}

void CoordinatorHTTPUI::HandleJobsListURI(HTTPRequestPtr& http_request,  // NOLINT
                                          TCPConnectionPtr& tcp_conn) {  // NOLINT
  LogRequest(http_request);
  HTTPResponseWriterPtr writer = InitOkResponse(http_request, tcp_conn);
  // Get job list from coordinator
  vector<JobDescriptor> jobs = coordinator_->active_jobs();
  uint64_t i = 0;
  TemplateDictionary dict("jobs_list");
  AddHeaderToTemplate(&dict, coordinator_->uuid(), NULL);
  AddFooterToTemplate(&dict);
  for (vector<JobDescriptor>::const_iterator jd_iter =
       jobs.begin();
       jd_iter != jobs.end();
       ++jd_iter) {
    TemplateDictionary* sect_dict = dict.AddSectionDictionary("JOB_DATA");
    sect_dict->SetIntValue("JOB_NUM", i);
    sect_dict->SetValue("JOB_ID", to_string(jd_iter->uuid()));
    sect_dict->SetValue("JOB_FRIENDLY_NAME", jd_iter->name());
    sect_dict->SetFormattedValue("JOB_ROOT_TASK_ID", "%lu",
                                 jd_iter->root_task().uid());
    sect_dict->SetValue("JOB_STATE",
                        ENUM_TO_STRING(JobDescriptor::JobState,
                                       jd_iter->state()));
    ++i;
  }
  string output;
  ExpandTemplate("src/webui/jobs_list.tpl", ctemplate::DO_NOT_STRIP, &dict,
                 &output);
  writer->write(output);
  FinishOkResponse(writer);
}

void CoordinatorHTTPUI::HandleJobURI(HTTPRequestPtr& http_request,  // NOLINT
                                     TCPConnectionPtr& tcp_conn) {  // NOLINT
  LogRequest(http_request);
  HTTPResponseWriterPtr writer = InitOkResponse(http_request, tcp_conn);
  // Get resource information from coordinator
  HTTPTypes::QueryParams &params = http_request->getQueryParams();
  string* job_id = FindOrNull(params, "id");
  if (!job_id) {
    ErrorResponse(HTTPTypes::RESPONSE_CODE_SERVER_ERROR, http_request,
                  tcp_conn);
    return;
  }
  JobDescriptor* jd_ptr = coordinator_->GetJob(
      JobIDFromString(*job_id));
  TemplateDictionary dict("job_status");
  if (jd_ptr) {
    dict.SetValue("JOB_ID", jd_ptr->uuid());
    dict.SetValue("JOB_NAME", jd_ptr->name());
    dict.SetValue("JOB_STATUS", ENUM_TO_STRING(JobDescriptor::JobState,
                                               jd_ptr->state()));
    dict.SetFormattedValue("JOB_ROOT_TASK_ID", "%lu",
                           jd_ptr->root_task().uid());
    if (jd_ptr->output_ids_size() > 0)
      dict.SetIntValue("JOB_NUM_OUTPUTS", jd_ptr->output_ids_size());
    else
      dict.SetIntValue("JOB_NUM_OUTPUTS", 1);
    for (RepeatedField<DataObjectID_t>::const_iterator out_iter =
         jd_ptr->output_ids().begin();
         out_iter != jd_ptr->output_ids().end();
         ++out_iter) {
      TemplateDictionary* out_dict = dict.AddSectionDictionary("JOB_OUTPUTS");
      out_dict->SetIntValue("JOB_OUTPUT_ID", *out_iter);
    }
    AddHeaderToTemplate(&dict, coordinator_->uuid(), NULL);
  } else {
    ErrorMessage_t err("Job not found.",
                       "The requested job does not exist or is unknown to "
                       "this coordinator.");
    AddHeaderToTemplate(&dict, coordinator_->uuid(), &err);
  }
  AddFooterToTemplate(&dict);
  string output;
  ExpandTemplate("src/webui/job_status.tpl", ctemplate::DO_NOT_STRIP,
                 &dict, &output);
  writer->write(output);
  FinishOkResponse(writer);
}

void CoordinatorHTTPUI::HandleResourcesListURI(HTTPRequestPtr& http_request,  // NOLINT
                                               TCPConnectionPtr& tcp_conn) {  // NOLINT
  LogRequest(http_request);
  HTTPResponseWriterPtr writer = InitOkResponse(http_request, tcp_conn);
  // Get resource information from coordinator
  const vector<ResourceDescriptor*> resources =
      coordinator_->associated_resources();
  uint64_t i = 0;
  TemplateDictionary dict("resources_list");
  AddHeaderToTemplate(&dict, coordinator_->uuid(), NULL);
  AddFooterToTemplate(&dict);
  for (vector<ResourceDescriptor*>::const_iterator rd_iter =
       resources.begin();
       rd_iter != resources.end();
       ++rd_iter) {
    TemplateDictionary* sect_dict = dict.AddSectionDictionary("RES_DATA");
    sect_dict->SetIntValue("RES_NUM", i);
    sect_dict->SetValue("RES_ID", to_string((*rd_iter)->uuid()));
    sect_dict->SetValue("RES_FRIENDLY_NAME", (*rd_iter)->friendly_name());
    sect_dict->SetValue("RES_STATE",
                        ENUM_TO_STRING(ResourceDescriptor::ResourceState,
                                       (*rd_iter)->state()));
    // N.B.: We make the assumption that only PU type resources are schedulable
    // here!
    if ((*rd_iter)->type() != ResourceDescriptor::RESOURCE_PU)
      sect_dict->AddSectionDictionary("RES_NON_SCHEDULABLE");
    ++i;
  }
  string output;
  ExpandTemplate("src/webui/resources_list.tpl", ctemplate::DO_NOT_STRIP, &dict,
                 &output);
  writer->write(output);
  FinishOkResponse(writer);
}

void CoordinatorHTTPUI::HandleResourceURI(HTTPRequestPtr& http_request,  // NOLINT
                                          TCPConnectionPtr& tcp_conn) {  // NOLINT
  LogRequest(http_request);
  HTTPResponseWriterPtr writer = InitOkResponse(http_request, tcp_conn);
  // Get resource information from coordinator
  HTTPTypes::QueryParams &params = http_request->getQueryParams();
  string* res_id = FindOrNull(params, "id");
  if (!res_id) {
    ErrorResponse(HTTPTypes::RESPONSE_CODE_SERVER_ERROR, http_request,
                  tcp_conn);
    return;
  }
  ResourceDescriptor* rd_ptr = coordinator_->GetResource(
      ResourceIDFromString(*res_id));
  TemplateDictionary dict("resource_status");
  if (rd_ptr) {
    dict.SetValue("RES_ID", rd_ptr->uuid());
    dict.SetValue("RES_FRIENDLY_NAME", rd_ptr->friendly_name());
    dict.SetValue("RES_TYPE", ENUM_TO_STRING(ResourceDescriptor::ResourceType,
                                             rd_ptr->type()));
    dict.SetValue("RES_STATUS",
                  ENUM_TO_STRING(ResourceDescriptor::ResourceState,
                                 rd_ptr->state()));
    dict.SetValue("RES_PARENT_ID", rd_ptr->parent());
    dict.SetIntValue("RES_NUM_CHILDREN", rd_ptr->children_size());
    AddHeaderToTemplate(&dict, coordinator_->uuid(), NULL);
  } else {
    VLOG(1) << "rd_ptr is: " << rd_ptr;
    ErrorMessage_t err("Resource not found.",
                       "The requested resource does not exist.");
    AddHeaderToTemplate(&dict, coordinator_->uuid(), &err);
  }
  AddFooterToTemplate(&dict);
  string output;
  ExpandTemplate("src/webui/resource_status.tpl", ctemplate::DO_NOT_STRIP,
                 &dict, &output);
  writer->write(output);
  FinishOkResponse(writer);
}

void CoordinatorHTTPUI::HandleResourcesTopologyURI(
    HTTPRequestPtr& http_request,  // NOLINT
    TCPConnectionPtr& tcp_conn) {  // NOLINT
  LogRequest(http_request);
  // Get resource topology from coordinator
  const ResourceTopologyNodeDescriptor& root_rtnd =
      coordinator_->local_resource_topology();
  // Return serialized resource topology
  HTTPResponseWriterPtr writer = InitOkResponse(http_request,
                                                tcp_conn);
  char *json = pb2json(root_rtnd);
  writer->write(json);
  FinishOkResponse(writer);
}

void CoordinatorHTTPUI::HandleInjectURI(HTTPRequestPtr& http_request,  // NOLINT
                                        TCPConnectionPtr& tcp_conn) {  // NOLINT
  LogRequest(http_request);
  HTTPResponseWriterPtr writer = InitOkResponse(http_request, tcp_conn);
  // Individual to this request
  if (http_request->getMethod() != "POST") {
    // return an error
    writer->write("POST a message to this URL to inject it.");
  } else {
    writer->write("ok");
  }
  FinishOkResponse(writer);
}

void CoordinatorHTTPUI::HandleJobStatusURI(HTTPRequestPtr& http_request,  // NOLINT
                                           TCPConnectionPtr& tcp_conn) {  // NOLINT
  LogRequest(http_request);
  HTTPResponseWriterPtr writer = InitOkResponse(http_request, tcp_conn);
  HTTPTypes::QueryParams &params = http_request->getQueryParams();
  string* job_id = FindOrNull(params, "id");
  if (!job_id) {
    ErrorResponse(HTTPTypes::RESPONSE_CODE_SERVER_ERROR, http_request,
                  tcp_conn);
    return;
  }
  TemplateDictionary dict("job_dtg");
  AddHeaderToTemplate(&dict, coordinator_->uuid(), NULL);
  AddFooterToTemplate(&dict);
  string output;
  if (job_id) {
    dict.SetValue("JOB_ID", *job_id);
    ExpandTemplate("src/webui/job_dtg.tpl", ctemplate::DO_NOT_STRIP, &dict,
                   &output);
  } else {
    output = "Please specify a job ID parameter.";
  }
  writer->write(output);
  FinishOkResponse(writer);
}

void CoordinatorHTTPUI::HandleJobDTGURI(HTTPRequestPtr& http_request,  // NOLINT
                                        TCPConnectionPtr& tcp_conn) {  // NOLINT
  LogRequest(http_request);

  HTTPTypes::QueryParams &params = http_request->getQueryParams();
  string* job_id = FindOrNull(params, "id");
  if (job_id) {
    // Get DTG from coordinator
    const JobDescriptor* jd = coordinator_->DescriptorForJob(*job_id);
    if (!jd) {
      // Job not found here
      VLOG(1) << "Requested DTG for non-existent job " << *job_id;
      ErrorResponse(HTTPTypes::RESPONSE_CODE_NOT_FOUND, http_request, tcp_conn);
      return;
    }
    // Return serialized DTG
    HTTPResponseWriterPtr writer = InitOkResponse(http_request,
                                                  tcp_conn);
    char *json = pb2json(*jd);
    writer->write(json);
    FinishOkResponse(writer);
  } else {
    ErrorResponse(HTTPTypes::RESPONSE_CODE_SERVER_ERROR, http_request,
                  tcp_conn);
    return;
  }
}

void CoordinatorHTTPUI::HandleReferenceURI(HTTPRequestPtr& http_request,  // NOLINT
                                           TCPConnectionPtr& tcp_conn) {  // NOLINT
  LogRequest(http_request);
  HTTPResponseWriterPtr writer = InitOkResponse(http_request, tcp_conn);
  // Get resource information from coordinator
  HTTPTypes::QueryParams &params = http_request->getQueryParams();
  string* ref_id = FindOrNull(params, "id");
  if (!ref_id) {
    ErrorResponse(HTTPTypes::RESPONSE_CODE_SERVER_ERROR, http_request,
                  tcp_conn);
    return;
  }
  ReferenceDescriptor* rd_ptr = coordinator_->GetReference(
      DataObjectIDFromString(*ref_id));
  TemplateDictionary dict("reference_view");
  if (rd_ptr) {
    dict.SetIntValue("REF_ID", rd_ptr->id());
    dict.SetValue("REF_TYPE", ENUM_TO_STRING(ReferenceDescriptor::ReferenceType,
                                             rd_ptr->type()));
    dict.SetValue("REF_SCOPE",
                  ENUM_TO_STRING(ReferenceDescriptor::ReferenceScope,
                                 rd_ptr->scope()));
    dict.SetIntValue("REF_NONDET", rd_ptr->non_deterministic());
    dict.SetIntValue("REF_SIZE", rd_ptr->size());
    dict.SetIntValue("REF_PRODUCER", rd_ptr->producing_task());
    AddHeaderToTemplate(&dict, coordinator_->uuid(), NULL);
  } else {
    ErrorMessage_t err("Reference or data object not found.",
                       "There exists no local reference for the requested "
                       "data object ID.");
    AddHeaderToTemplate(&dict, coordinator_->uuid(), &err);
  }
  AddFooterToTemplate(&dict);
  string output;
  ExpandTemplate("src/webui/reference_view.tpl", ctemplate::DO_NOT_STRIP,
                 &dict, &output);
  writer->write(output);
  FinishOkResponse(writer);
}

void CoordinatorHTTPUI::HandleTaskURI(HTTPRequestPtr& http_request,  // NOLINT
                                      TCPConnectionPtr& tcp_conn) {  // NOLINT
  LogRequest(http_request);
  HTTPResponseWriterPtr writer = InitOkResponse(http_request, tcp_conn);
  // Get resource information from coordinator
  HTTPTypes::QueryParams &params = http_request->getQueryParams();
  string* task_id = FindOrNull(params, "id");
  if (!task_id) {
    ErrorResponse(HTTPTypes::RESPONSE_CODE_SERVER_ERROR, http_request,
                  tcp_conn);
    return;
  }
  TaskDescriptor* td_ptr = coordinator_->GetTask(
      TaskIDFromString(*task_id));
  TemplateDictionary dict("task_status");
  if (td_ptr) {
    dict.SetFormattedValue("TASK_ID", "%lu", td_ptr->uid());
    //dict.SetValue("TASK_NAME", td_ptr->name());
    dict.SetValue("TASK_STATUS", ENUM_TO_STRING(TaskDescriptor::TaskState,
                                                td_ptr->state()));
    AddHeaderToTemplate(&dict, coordinator_->uuid(), NULL);
  } else {
    ErrorMessage_t err("Task not found.",
                       "The requested task does not exist or is unknown to "
                       "this coordinator.");
    AddHeaderToTemplate(&dict, coordinator_->uuid(), &err);
  }
  AddFooterToTemplate(&dict);
  string output;
  ExpandTemplate("src/webui/task_status.tpl", ctemplate::DO_NOT_STRIP,
                 &dict, &output);
  writer->write(output);
  FinishOkResponse(writer);
}

void CoordinatorHTTPUI::HandleShutdownURI(HTTPRequestPtr& http_request,  // NOLINT
                                          TCPConnectionPtr& tcp_conn) {  // NOLINT
  LogRequest(http_request);
  HTTPResponseWriterPtr writer = InitOkResponse(http_request, tcp_conn);
  string reason = "HTTP request from " + tcp_conn->getRemoteIp().to_string();
  // Make the HTTP server inactive, so that the coordinator does not try to shut
  // it down.
  active_ = false;
  // Now initiate coordinator shutdown
  coordinator_->Shutdown(reason);
  writer->write("Shutdown for coordinator initiated.");
  FinishOkResponse(writer);
  // Now shut down the HTTP server itself
  Shutdown(true);
}

HTTPResponseWriterPtr CoordinatorHTTPUI::InitOkResponse(
    HTTPRequestPtr http_request,
    TCPConnectionPtr tcp_conn) {
  HTTPResponseWriterPtr writer = HTTPResponseWriter::create(
      tcp_conn, *http_request, boost::bind(&TCPConnection::finish,
                                           tcp_conn));
  HTTPResponse& r = writer->getResponse();
  r.setStatusCode(HTTPTypes::RESPONSE_CODE_OK);
  r.setStatusMessage(HTTPTypes::RESPONSE_MESSAGE_OK);
  // Hack to allow file:// access
  r.addHeader("Access-Control-Allow-Origin", "*");
  return writer;
}

void CoordinatorHTTPUI::ErrorResponse(
    const unsigned int error_code,
    HTTPRequestPtr http_request,
    TCPConnectionPtr tcp_conn) {
  HTTPResponseWriterPtr writer = HTTPResponseWriter::create(
      tcp_conn, *http_request, boost::bind(&TCPConnection::finish,
                                           tcp_conn));
  HTTPResponse& r = writer->getResponse();
  r.setStatusCode(error_code);
  //r.setStatusMessage("test");
  writer->send();
}

void CoordinatorHTTPUI::FinishOkResponse(HTTPResponseWriterPtr writer) {
  writer->send();
}

void CoordinatorHTTPUI::LogRequest(const HTTPRequestPtr& http_request) {
  LOG(INFO) << "[HTTPREQ] Serving " << http_request->getResource();
}

void CoordinatorHTTPUI::Init(uint16_t port) {
  try {
    // Fail if we are not assured that no existing server object is stored.
    if (coordinator_http_server_ != NULL) {
      LOG(FATAL) << "Trying to initialized an HTTP server that has already "
                 << "been initialized!";
    }
    // Otherwise, make such an object and store it.
    coordinator_http_server_.reset(new HTTPServer(port));
    // Bind handlers for different kinds of entry points
    // Root URI
    coordinator_http_server_->addResource("/", boost::bind(
        &CoordinatorHTTPUI::HandleRootURI, this, _1, _2));
    // Root URI
    coordinator_http_server_->addResource("/favicon.ico", boost::bind(
        &CoordinatorHTTPUI::HandleFaviconURI, this, _1, _2));
    // Job submission
    coordinator_http_server_->addResource("/jobs/", boost::bind(
        &CoordinatorHTTPUI::HandleJobsListURI, this, _1, _2));
    // Job submission
    coordinator_http_server_->addResource("/job/submit/", boost::bind(
        &CoordinatorHTTPUI::HandleJobSubmitURI, this, _1, _2));
    // Job status
    coordinator_http_server_->addResource("/job/status/", boost::bind(
        &CoordinatorHTTPUI::HandleJobURI, this, _1, _2));
    // Job task graph visualization
    coordinator_http_server_->addResource("/job/dtg-view/", boost::bind(
        &CoordinatorHTTPUI::HandleJobStatusURI, this, _1, _2));
    // Job task graph
    coordinator_http_server_->addResource("/job/dtg/", boost::bind(
        &CoordinatorHTTPUI::HandleJobDTGURI, this, _1, _2));
    // Resource list
    coordinator_http_server_->addResource("/resources/", boost::bind(
        &CoordinatorHTTPUI::HandleResourcesListURI, this, _1, _2));
    // Resource topology
    coordinator_http_server_->addResource("/resources/topology/", boost::bind(
        &CoordinatorHTTPUI::HandleResourcesTopologyURI, this, _1, _2));
    // Resource page
    coordinator_http_server_->addResource("/resource/", boost::bind(
        &CoordinatorHTTPUI::HandleResourceURI, this, _1, _2));
    // Message injection
    coordinator_http_server_->addResource("/inject/", boost::bind(
        &CoordinatorHTTPUI::HandleInjectURI, this, _1, _2));
    // Reference status
    coordinator_http_server_->addResource("/ref/", boost::bind(
        &CoordinatorHTTPUI::HandleReferenceURI, this, _1, _2));
    // Task status
    coordinator_http_server_->addResource("/task/", boost::bind(
        &CoordinatorHTTPUI::HandleTaskURI, this, _1, _2));
    // Shutdown request
    coordinator_http_server_->addResource("/shutdown/", boost::bind(
        &CoordinatorHTTPUI::HandleShutdownURI, this, _1, _2));
    // Start the HTTP server
    coordinator_http_server_->start();  // spawns a thread!
    LOG(INFO) << "Coordinator HTTP interface up!";
  } catch(const std::exception& e) {
    LOG(ERROR) << "Failed running the coordinator's HTTP UI due to "
               << e.what();
  }
}

void CoordinatorHTTPUI::Shutdown(bool block) {
  LOG(INFO) << "Coordinator HTTP UI server shutting down on request.";
  coordinator_http_server_->stop(block);
  VLOG(1) << "HTTP UI shut down.";
}

}  // namespace webui
}  // namespace firmament
