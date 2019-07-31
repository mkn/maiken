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
#include "maiken.hpp"
#include "maiken/dist.hpp"

namespace maiken {

class DistLinker {
 public:
  static void send(const kul::File &bin) {
    (void)bin;
#if defined(_MKN_WITH_MKN_RAM_) && defined(_MKN_WITH_IO_CEREAL_)
    std::vector<std::shared_ptr<maiken::dist::Post>> posts;
    auto post_lambda = [](const dist::Host &host, const kul::File &bin) {
      kul::io::BinaryReader br(bin);
      dist::Blob b;
      b.files_left = 1;
      b.file = bin.real();
      size_t red = 0;
      do {
        bzero(b.c1, dist::BUFF_SIZE / 2);
        red = b.len = br.read(b.c1, dist::BUFF_SIZE / 2);
        if (red == 0) {
          b.last_packet = 1;
          b.files_left = 0;
        }
        std::ostringstream ss(std::ios::out | std::ios::binary);
        {
          cereal::PortableBinaryOutputArchive oarchive(ss);
          oarchive(b);
        }
        auto link = std::make_shared<maiken::dist::Post>(
            maiken::dist::RemoteCommandManager::INST().build_link_request(ss.str()));
        link->send(host);
      } while (red > 0);
    };

    auto &hosts(maiken::dist::RemoteCommandManager::INST().hosts());
    size_t threads = hosts.size();
    kul::ChroncurrentThreadPool<> ctp(threads, 1, 1000000, 1000);
    auto post_ex = [&](const kul::Exception &e) {
      ctp.stop().interrupt();
      throw e;
    };
    for (size_t i = 0; i < threads; i++) {
      ctp.async(std::bind(post_lambda, std::ref(hosts[i]), std::ref(bin)), post_ex);
    }
    ctp.finish(10000000);  // 10 milliseconds
    ctp.rethrow();
#endif  //  _MKN_WITH_MKN_RAM_) && defined(_MKN_WITH_IO_CEREAL_)
  }
};

class Executioner : public Constants {
  friend class Application;

  static CompilerProcessCapture build_exe(const kul::hash::set::String &objects,
                                          const std::string &main, const std::string &out,
                                          const kul::Dir outD, Application &app) {
    const std::string &file = main;
    const std::string &fileType = file.substr(file.rfind(".") + 1);

    if (!(*app.files().find(fileType)).second.count(STR_COMPILER))
      KEXIT(1, "No compiler found for filetype " + fileType);
    if (!AppVars::INSTANCE().dryRun() && kul::LogMan::INSTANCE().inf() &&
        !app.libraries().empty()) {
      KOUT(NON) << "LIBRARIES";
      for (const std::string &s : app.libraries()) KOUT(NON) << "\t" << s;
    }
    if (!AppVars::INSTANCE().dryRun() && kul::LogMan::INSTANCE().inf() &&
        !app.libraryPaths().empty()) {
      KOUT(NON) << "LIBRARY PATHS";
      for (const std::string &s : app.libraryPaths()) KOUT(NON) << "\t" << s;
    }
    try {
      std::string linker = app.fs[fileType][STR_LINKER];
      std::string linkEnd;
      if (app.ro) linkEnd = AppVars::INSTANCE().linker();
      if (!AppVars::INSTANCE().allinker().empty()) linkEnd += " " + AppVars::INSTANCE().allinker();
      if (!app.lnk.empty()) linkEnd += " " + app.lnk;
      if (!AppVars::INSTANCE().dryRun() && kul::LogMan::INSTANCE().inf() && linkEnd.size())
        KOUT(NON) << "LINKER ARGUMENTS\n\t" << linkEnd;
      std::string bin(AppVars::INSTANCE().dryRun() ? kul::File(outD.join(out)).esc()
                                                   : kul::File(outD.join(out)).escm());
      std::vector<std::string> obV;
      for (const auto &o : objects) obV.emplace_back(o);
      const std::string &base(Compilers::INSTANCE().base(
          (*(*app.files().find(fileType)).second.find(STR_COMPILER)).second));
      if (app.cLnk.count(base)) linkEnd += " " + app.cLnk[base];
      auto *comp = Compilers::INSTANCE().get(base);
      auto linkOpt(comp->linkerOptimizationBin(AppVars::INSTANCE().optimise()));
      if (!linkOpt.empty()) linker += " " + linkOpt;
      auto linkDbg(comp->linkerDebugBin(AppVars::INSTANCE().debug()));
      if (!linkDbg.empty()) linker += " " + linkDbg;
      const CompilerProcessCapture &cpc =
          comp->buildExecutable(linker, linkEnd, obV, app.libraries(), app.libraryPaths(), bin,
                                app.m, AppVars::INSTANCE().dryRun());
      if (AppVars::INSTANCE().dryRun())
        KOUT(NON) << cpc.cmd();
      else {
        app.checkErrors(cpc);
        KOUT(INF) << cpc.cmd();
        KOUT(NON) << "Creating bin: " << kul::File(cpc.file()).real();
#if defined(_MKN_WITH_MKN_RAM_) && defined(_MKN_WITH_IO_CEREAL_)
        if (AppVars::INSTANCE().nodes()) DistLinker::send(cpc.file());
#endif
      }
      return cpc;
    } catch (const CompilerNotFoundException &e) {
      KEXCEPTION("UNSUPPORTED COMPILER EXCEPTION");
    }
  }
};
}  // namespace maiken

void maiken::Application::buildExecutable(const kul::hash::set::String &objects)
    KTHROW(kul::Exception) {
  const std::string &file = main;
  const std::string &fileType = file.substr(file.rfind(".") + 1);
  if (fs.count(fileType) == 0)
    KEXIT(1, "Unable to handle artifact: \"" + file + "\" - type is not in file list");
  const std::string oType("." + (*AppVars::INSTANCE().envVars().find("MKN_OBJ")).second);
  kul::Dir objD(buildDir().join("obj"));
  const std::string &name(out.empty() ? project().root()[STR_NAME].Scalar() : out);
  const kul::File source(main);
  std::stringstream ss, os;
  ss << std::hex << std::hash<std::string>()(source.real());
  os << ss.str() << "-" << source.name() << oType;
  kul::File object(os.str(), objD);
  kul::Dir tmpD(buildDir().join("tmp"));
  kul::File tbject(os.str(), tmpD);
  if (!tbject)
    KERR << "Source expected not found (ignoring) " << tbject;
  else
    tbject.mv(objD);
  Executioner::build_exe(objects, main, name, kul::Dir(inst ? inst.real() : buildDir()), *this);
  object.mv(tmpD);
  object.mv(tmpD);
}

void maiken::Application::buildTest(const kul::hash::set::String &objects) KTHROW(kul::Exception) {
  const std::string oType("." + (*AppVars::INSTANCE().envVars().find("MKN_OBJ")).second);
  std::vector<std::pair<Source, std::string>> source_objects;
  std::vector<std::pair<std::string, std::string>> test_objects;
  kul::Dir objD(buildDir().join("obj")), testsD(buildDir().join("test")),
      tmpD(buildDir().join("tmp"));
  objD.mk();
  for (const auto &p : tests) {
    const std::string &file = p.first;
    const std::string &fileType = file.substr(file.rfind(".") + 1);
    if (fs.count(fileType) == 0) continue;
    if (!testsD) testsD.mk();
    if (!tmpD) tmpD.mk();
    const kul::File source(p.first);
    std::stringstream ss, os;
    ss << std::hex << std::hash<std::string>()(source.real());
    os << ss.str() << "-" << source.name() << oType;
    kul::File object(os.str(), objD);
    test_objects.push_back(std::make_pair(p.first, os.str()));
    source_objects.emplace_back(
         (AppVars::INSTANCE().dryRun() ? source.esc() : source.escm()),
          AppVars::INSTANCE().dryRun() ? object.esc() : object.escm());
  }
  {
    std::vector<kul::File> cacheFiles;
    kul::hash::set::String cobjects;
    compile(source_objects, cobjects, cacheFiles);
  }
  for (const auto &to : test_objects) kul::File(to.second, objD).mv(tmpD);
  for (const auto &to : test_objects) {
    kul::File(to.second, tmpD).mv(objD);
    kul::hash::set::String cobjects = objects;
    cobjects.insert(kul::File(to.second, objD).escm());
    Executioner::build_exe(cobjects, to.first, to.second, testsD, *this);
    kul::File(to.second, objD).mv(tmpD);
  }
}

maiken::CompilerProcessCapture maiken::Application::buildLibrary(
    const kul::hash::set::String &objects) KTHROW(kul::Exception) {
  if (fs.count(lang) > 0) {
    if (m == compiler::Mode::NONE) m = compiler::Mode::SHAR;
    if (!(*files().find(lang)).second.count(STR_COMPILER))
      KEXIT(1, "No compiler found for filetype " + lang);
    std::string linker = fs[lang][STR_LINKER];
    std::string linkEnd;
    if (ro) linkEnd = AppVars::INSTANCE().linker();
    if (!AppVars::INSTANCE().allinker().empty()) linkEnd += " " + AppVars::INSTANCE().allinker();
    if (!lnk.empty()) linkEnd += " " + lnk;
    if (!AppVars::INSTANCE().dryRun() && kul::LogMan::INSTANCE().inf() && linkEnd.size())
      KOUT(NON) << "LINKER ARGUMENTS\n\t" << linkEnd;
    if (m == compiler::Mode::STAT) linker = fs[lang][STR_ARCHIVER];
    kul::Dir outD(inst ? inst.real() : buildDir());
    std::string lib(baseLibFilename());
    lib = AppVars::INSTANCE().dryRun() ? kul::File(lib, outD).esc() : kul::File(lib, outD).escm();
    std::vector<std::string> obV;
    for (const auto &o : objects) obV.emplace_back(o);
    const std::string &base(
        Compilers::INSTANCE().base((*(*files().find(lang)).second.find(STR_COMPILER)).second));
    if (cLnk.count(base)) linkEnd += " " + cLnk[base];
    auto *comp = Compilers::INSTANCE().get(base);
    auto linkOpt(comp->linkerOptimizationLib(AppVars::INSTANCE().optimise()));
    if (!linkOpt.empty()) linker += " " + linkOpt;
    auto linkDbg(comp->linkerDebugLib(AppVars::INSTANCE().debug()));
    if (!linkDbg.empty()) linker += " " + linkDbg;
    const CompilerProcessCapture &cpc = comp->buildLibrary(
        linker, linkEnd, obV, libraries(), libraryPaths(), lib, m, AppVars::INSTANCE().dryRun());
    if (AppVars::INSTANCE().dryRun())
      KOUT(NON) << cpc.cmd();
    else {
      checkErrors(cpc);
      KOUT(INF) << cpc.cmd();
      KOUT(NON) << "Creating lib: " << kul::File(cpc.file()).real();
#if defined(_MKN_WITH_MKN_RAM_) && defined(_MKN_WITH_IO_CEREAL_)
      if (AppVars::INSTANCE().nodes()) DistLinker::send(cpc.file());
#endif
    }
    return cpc;
  } else
    KEXCEPTION("Unable to handle artifact: \"" + lang + "\" - type is not in file list");
}
