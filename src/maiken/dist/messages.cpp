/**
Copyright (c) 2017, Philip Deegan.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    * Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the following disclaimer
in the documentation and/or other materials provided with the
distribution.
    * Neither the name of Philip Deegan nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#if defined(_MKN_WITH_MKN_RAM_) && defined(_MKN_WITH_IO_CEREAL_)

#include "maiken/dist.hpp"

void maiken::dist::SetupRequest::do_response_for(
    const kul::http::A1_1Request &req, Post &p, Sessions &sessions,
    kul::http::_1_1Response &resp) {
  KLOG(INF) << req.ip();

  YAML::Node root;
  bool expected = false;
  bool success =
      1;  // std::atomic_compare_exchange_strong(&busy, &expected, true);
  if (success)
    root["status"] = 0;
  else {
    root["status"] = 1;
    root["message"] = "NODE BUSY";
  }
  YAML::Emitter out;
  out << root;
  p.release();
  sessions[req.ip()].reset_setup(this);
  sessions[req.ip()].setup_ptr()->m_args.erase(STR_NODES);
  sessions[req.ip()].set_apps(
      maiken::Application::CREATE(sessions[req.ip()].setup_ptr()->m_args));

  resp.withBody(std::string(out.c_str()));
}

void maiken::dist::CompileRequest::do_response_for(
    const kul::http::A1_1Request &req, Post &p, Sessions &sessions,
    kul::http::_1_1Response &resp) {
  KLOG(INF) << req.ip();
  kul::env::CWD(this->m_directory);

  std::vector<kul::File> cacheFiles;
  sessions[req.ip()].apps_vector()[0]->compile(
      this->m_src_obj, sessions[req.ip()].objects, cacheFiles);

  YAML::Node root;
  bool expected = false;
  bool success = 1;
  if (success)
    root["status"] = 0;
  else {
    root["status"] = 1;
    root["message"] = "NODE BUSY";
  }
  root["files"] = this->m_src_obj.size();
  YAML::Emitter out;
  out << root;
  sessions[req.ip()].m_src_obj = std::move(this->m_src_obj);
  resp.withBody(std::string(out.c_str()));
}

void maiken::dist::DownloadRequest::do_response_for(
    const kul::http::A1_1Request &req, Post &p, Sessions &sessions,
    kul::http::_1_1Response &resp) {
  KLOG(INF) << req.ip();

  auto &src_obj = sessions[req.ip()].m_src_obj;
  if (!sessions[req.ip()].binary_reader) {
    if (!src_obj.empty()) {
      auto &pair = src_obj[0];
      kul::File bin(pair.second);
      sessions[req.ip()].binary_reader =
          std::make_unique<kul::io::BinaryReader>(bin);
    }
  }

  Blob b;
  b.files_left = src_obj.size();
  bzero(b.c1, BUFF_SIZE);
  b.file = src_obj[0].second;
  auto &br(*sessions[req.ip()].binary_reader.get());
  size_t red = b.len = br.read(b.c1, BUFF_SIZE);
  if (red == 0) {
    src_obj.erase(src_obj.begin());
    b.files_left = src_obj.size();
    sessions[req.ip()].binary_reader.reset();
    // if (b.files_left == 0) sessions.erase(req.ip());
    b.last_packet = 1;
  }
  std::ostringstream ss(std::ios::out | std::ios::binary);
  {
    cereal::PortableBinaryOutputArchive oarchive(ss);
    oarchive(b);
  }
  KLOG(INF) << b.len;
  KLOG(INF) << BUFF_SIZE;
  KLOG(INF) << ss.str().size();
  resp.withBody(ss.str());
}

#endif  // _MKN_WITH_MKN_RAM_ && _MKN_WITH_IO_CEREAL_
