#pragma once

#include <ARA_Library/Dispatch/ARAHostDispatch.h>
#include <atomic>
#include <vector>

// Ordinary SDK host implementation. This file and its .cpp have no AAP API dependency.
class ExistingARAHostServices : public ARA::Host::AudioAccessControllerInterface,
                                public ARA::Host::ArchivingControllerInterface {
public:
    int sourceIdentity{};
    std::atomic<int> audioReads{};
    std::vector<ARA::ARAByte> archive;
    ARA::ARAPersistentID archiveID{};
    ARA::Host::DocumentControllerHostInstance instance{this, this};

    ARA::ARAAudioReaderHostRef createAudioReaderForSource(ARA::ARAAudioSourceHostRef, bool) noexcept override;
    bool readAudioSamples(ARA::ARAAudioReaderHostRef, ARA::ARASamplePosition, ARA::ARASampleCount, void* const[]) noexcept override;
    void destroyAudioReader(ARA::ARAAudioReaderHostRef) noexcept override;
    ARA::ARASize getArchiveSize(ARA::ARAArchiveReaderHostRef) noexcept override;
    bool readBytesFromArchive(ARA::ARAArchiveReaderHostRef, ARA::ARASize, ARA::ARASize, ARA::ARAByte[]) noexcept override;
    bool writeBytesToArchive(ARA::ARAArchiveWriterHostRef, ARA::ARASize, ARA::ARASize, const ARA::ARAByte[]) noexcept override;
    void notifyDocumentArchivingProgress(float) noexcept override {}
    void notifyDocumentUnarchivingProgress(float) noexcept override {}
    ARA::ARAPersistentID getDocumentArchiveID(ARA::ARAArchiveReaderHostRef) noexcept override { return archiveID; }
};

void exerciseExistingARAHost(const ARA::ARADocumentControllerInstance*,
        const ARA::ARAPlugInExtensionInstance*, ExistingARAHostServices&);
