#include "upstream-scenarios.h"
#include "aap-example-entry.h"
#include "TestCases.h"
#include <sstream>

std::string sample_scenarios::runUpstreamScenarios(aap::PluginClientConnectionList* connections, const std::string& pluginID) {
    // Run the eight upstream TestHost scenarios supported by the AAP companion.
    // Keep each isolated from the visible editor and report every result.
    struct Scenario { const char* name; void (*run)(PlugInEntry*, const AudioFileList&); };
    const Scenario scenarios[]{
        {"Property updates", testPropertyUpdates}, {"Content updates", testContentUpdates},
        {"Content reading", testContentReading}, {"Modification cloning", testModificationCloning},
        {"Archiving", testArchiving}, {"Split archives", testSplitArchives},
        {"Drag and drop", testDragAndDrop}, {"Processing algorithms", testProcessingAlgorithms}};
    std::ostringstream report;
    report << "\nARA_Examples scenario coverage:\n";
    bool passed = true;
    const auto audio = createDummyAudioFiles(2);
    for (const auto& scenario : scenarios) {
        try {
            AAPEntry entry(connections, pluginID);
            const auto factory = entry.getARAFactory();
            bool hasNotes = false;
            for (size_t i = 0; i < factory->analyzeableContentTypesCount; ++i)
                hasNotes |= factory->analyzeableContentTypes[i] == ARA::kARAContentTypeNotes;
            if (!hasNotes) throw std::runtime_error("plug-in does not expose note analysis");
            if (scenario.run == testProcessingAlgorithms) {
                TestHost probe;
                auto* document = probe.addDocument("Algorithm capability check", &entry);
                if (probe.getDocumentController(document)->getProcessingAlgorithmsCount() == 0)
                    throw std::runtime_error("plug-in does not expose processing algorithms");
            }
            scenario.run(&entry, audio);
            report << "PASS: " << scenario.name << '\n';
        } catch (const std::exception& e) {
            passed = false;
            report << "FAIL: " << scenario.name << " — " << e.what() << '\n';
        }
    }
    report << "Upstream scenarios: " << (passed ? "PASS (8/8)" : "FAIL") << '\n';
    return report.str();
}
