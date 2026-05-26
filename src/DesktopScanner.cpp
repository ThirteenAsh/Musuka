#include "DesktopScanner.h"

#include "ImageUtil.h"
#include "Util.h"

#include <algorithm>
#include <filesystem>
#include <map>

namespace fs = std::filesystem;

namespace musuka {

namespace {

DesktopObject MakeVirtualObject(DesktopObjectType type) {
    DesktopObject object;
    object.type = type;
    if (type == DesktopObjectType::ThisPC) {
        object.name = L"此电脑";
        object.shellId = L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}";
    } else {
        object.name = L"回收站";
        object.shellId = L"::{645FF040-5081-101B-9F08-00AA002F954E}";
    }
    object.id = MakeObjectId(object.name, ObjectStableKey(object));
    object.includeInDesktop = true;
    return object;
}

std::wstring ExistingKey(const DesktopObject& object) {
    return NormalizePathForCompare(ObjectStableKey(object));
}

bool CandidateHasOriginalPath(const DesktopObject& object, const std::wstring& originalPath) {
    const std::wstring normalized = NormalizePathForCompare(originalPath);
    return std::any_of(object.candidates.begin(), object.candidates.end(), [&](const ImageCandidate& candidate) {
        return NormalizePathForCompare(candidate.originalPath) == normalized;
    });
}

bool HasOriginalIconCandidate(const DesktopObject& object) {
    return std::any_of(object.candidates.begin(), object.candidates.end(), [](const ImageCandidate& candidate) {
        return candidate.originalIcon;
    });
}

} // namespace

bool DesktopScanner::ScanAndPrepare(AppConfig& config, std::wstring& error, std::wstring& warning) {
    error.clear();
    warning.clear();

    if (!DirectoryExists(config.desktopPath)) {
        error = L"桌面路径不存在。";
        return false;
    }

    std::vector<DesktopObject> scanned;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(config.desktopPath, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            break;
        }

        DesktopObject object;
        if (entry.is_directory(ec)) {
            object.type = DesktopObjectType::Folder;
            object.name = entry.path().filename().wstring();
            object.path = NormalizePathForCompare(entry.path().wstring());
        } else if (entry.is_regular_file(ec) && ExtensionLower(entry.path().wstring()) == L".lnk") {
            object.type = DesktopObjectType::Shortcut;
            object.name = entry.path().stem().wstring();
            object.path = NormalizePathForCompare(entry.path().wstring());
        } else {
            continue;
        }

        object.id = MakeObjectId(object.name, ObjectStableKey(object));
        object.includeInDesktop = true;
        scanned.push_back(std::move(object));
    }

    scanned.push_back(MakeVirtualObject(DesktopObjectType::ThisPC));
    scanned.push_back(MakeVirtualObject(DesktopObjectType::RecycleBin));

    std::map<std::wstring, DesktopObject> existingByKey;
    for (auto& object : config.objects) {
        existingByKey[ExistingKey(object)] = object;
    }

    std::vector<DesktopObject> merged;
    for (auto& object : scanned) {
        auto it = existingByKey.find(ExistingKey(object));
        if (it != existingByKey.end()) {
            DesktopObject preserved = it->second;
            preserved.name = object.name;
            preserved.path = object.path;
            preserved.shellId = object.shellId;
            preserved.type = object.type;
            if (preserved.id.empty()) {
                preserved.id = object.id;
            }
            InitializeObjectImages(preserved, warning);
            merged.push_back(std::move(preserved));
        } else {
            InitializeObjectImages(object, warning);
            merged.push_back(std::move(object));
        }
    }

    config.objects = std::move(merged);
    return true;
}

void DesktopScanner::InitializeObjectImages(DesktopObject& object, std::wstring& warning) {
    if (object.id.empty()) {
        object.id = MakeObjectId(object.name, ObjectStableKey(object));
    }

    const std::wstring objectDir = CombinePath(GetIconsDirectory(), object.id);
    EnsureDirectory(objectDir);

    const std::wstring originalIconPath = CombinePath(objectDir, L"original_icon.png");
    if (!FileExists(originalIconPath)) {
        HICON icon = LoadShellIconForObject(object, true);
        if (icon) {
            SaveHIconAsPng(icon, originalIconPath);
            DestroyIcon(icon);
        }
    }

    if (!HasOriginalIconCandidate(object) && FileExists(originalIconPath)) {
        ImageCandidate candidate;
        candidate.displayName = L"原始图标";
        candidate.originalPath = object.path.empty() ? object.shellId : object.path;
        candidate.internalPath = ToAppRelativePath(originalIconPath);
        candidate.originalIcon = true;
        object.candidates.insert(object.candidates.begin(), std::move(candidate));
        object.selectedCandidate = 0;
    }

    const std::wstring defaultDir = GetDefaultImageDirectory();
    if (!DirectoryExists(defaultDir)) {
        if (warning.find(L"default_image") == std::wstring::npos) {
            warning += L"default_image 目录不存在，默认图片为空。\n";
        }
    } else {
        const auto defaultImages = EnumerateImageFiles(defaultDir, true);
        if (defaultImages.empty()) {
            if (warning.find(L"默认图片为空") == std::wstring::npos) {
                warning += L"default_image 目录中没有可用图片。\n";
            }
        }
        for (const auto& imagePath : defaultImages) {
            if (CandidateHasOriginalPath(object, imagePath)) {
                continue;
            }
            std::wstring relative;
            std::wstring copyError;
            if (CopyFileToInternal(imagePath, objectDir, L"default", relative, copyError)) {
                ImageCandidate candidate;
                candidate.displayName = FileNameFromPath(imagePath);
                candidate.originalPath = imagePath;
                candidate.internalPath = relative;
                candidate.originalIcon = false;
                object.candidates.push_back(std::move(candidate));
            }
        }
    }

    if (object.selectedCandidate < 0 ||
        object.selectedCandidate >= static_cast<int>(object.candidates.size())) {
        object.selectedCandidate = object.candidates.empty() ? -1 : 0;
    }
}

} // namespace musuka

