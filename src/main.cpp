#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/binding/GJDifficultySprite.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/PauseLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

using namespace geode::prelude;

namespace {
    constexpr float kDifficultyFaceScale = 0.55f;
    constexpr float kMinimumDifficultyFaceScale = 0.44f;
    constexpr float kCollisionMargin = 3.f;
    constexpr float kPositionGap = 6.f;
    constexpr float kSafeInset = 4.f;
    constexpr float kMinimumObstacleSize = 1.f;
    constexpr int kMaximumObstacleDepth = 5;

    struct PlacementResult {
        CCPoint position;
        float scale;
        size_t candidateIndex;
        bool preferredWasFree;
        bool usedFallback;
    };

    char const* difficultyName(int difficulty) {
        switch (difficulty) {
            case -1: return "Unrated";
            case 0: return "Auto";
            case 1: return "Easy";
            case 2: return "Normal";
            case 3: return "Hard";
            case 4: return "Harder";
            case 5: return "Insane";
            case 6: return "Hard Demon";
            case 7: return "Easy Demon";
            case 8: return "Medium Demon";
            case 9: return "Insane Demon";
            case 10: return "Extreme Demon";
            default: return "Unknown";
        }
    }

    int difficultyForLevel(GJGameLevel* level) {
        if (level->m_autoLevel) {
            return static_cast<int>(GJDifficulty::Auto);
        }

        auto const rawDifficulty = static_cast<int>(level->m_difficulty);

        // Official main demons are all hard demons. Their demon subtype can retain
        // the default value used by a newly constructed GJGameLevel.
        if (level->m_levelType == GJLevelType::Main &&
            rawDifficulty == static_cast<int>(GJDifficulty::Demon)) {
            return static_cast<int>(GJDifficulty::Demon);
        }

        if (level->m_demon.value() != 0) {
            return GJGameLevel::demonIconForDifficulty(
                static_cast<DemonDifficultyType>(level->m_demonDifficulty)
            );
        }

        // Online levels may carry the voters' average separately from the cached
        // enum. GD exposes the same calculation through getAverageDifficulty().
        if (level->m_ratingsSum != 0) {
            auto const averageDifficulty = level->getAverageDifficulty();
            if (averageDifficulty >= static_cast<int>(GJDifficulty::Easy) &&
                averageDifficulty <= static_cast<int>(GJDifficulty::Insane)) {
                return averageDifficulty;
            }
        }

        if (rawDifficulty >= static_cast<int>(GJDifficulty::NA) &&
            rawDifficulty <= static_cast<int>(GJDifficulty::DemonExtreme)) {
            // GJGameLevel defaults to Auto even before a level has a rating. The
            // explicit m_autoLevel flag is what distinguishes a real Auto level.
            if (rawDifficulty == static_cast<int>(GJDifficulty::Auto)) {
                return static_cast<int>(GJDifficulty::NA);
            }
            return rawDifficulty;
        }

        return static_cast<int>(GJDifficulty::NA);
    }

    bool getRectInPauseSpace(PauseLayer* pauseLayer, CCNode* node, CCRect& result) {
        auto* parent = node ? node->getParent() : nullptr;
        if (!pauseLayer || !node || !parent) {
            return false;
        }

        auto const parentRect = node->boundingBox();
        if (parentRect.size.width < kMinimumObstacleSize ||
            parentRect.size.height < kMinimumObstacleSize) {
            return false;
        }

        std::array<CCPoint, 4> const corners {{
            parentRect.origin,
            {parentRect.origin.x + parentRect.size.width, parentRect.origin.y},
            {parentRect.origin.x, parentRect.origin.y + parentRect.size.height},
            {parentRect.origin.x + parentRect.size.width, parentRect.origin.y + parentRect.size.height},
        }};

        auto minX = std::numeric_limits<float>::max();
        auto minY = std::numeric_limits<float>::max();
        auto maxX = std::numeric_limits<float>::lowest();
        auto maxY = std::numeric_limits<float>::lowest();

        for (auto const& corner : corners) {
            auto const localPoint = pauseLayer->convertToNodeSpace(
                parent->convertToWorldSpace(corner)
            );
            minX = std::min(minX, localPoint.x);
            minY = std::min(minY, localPoint.y);
            maxX = std::max(maxX, localPoint.x);
            maxY = std::max(maxY, localPoint.y);
        }

        if (!std::isfinite(minX) || !std::isfinite(minY) ||
            !std::isfinite(maxX) || !std::isfinite(maxY) ||
            maxX - minX < kMinimumObstacleSize ||
            maxY - minY < kMinimumObstacleSize) {
            return false;
        }

        result.origin = CCPoint(minX, minY);
        result.size = CCSize(maxX - minX, maxY - minY);
        return true;
    }

    void collectRelevantObstacles(
        PauseLayer* pauseLayer,
        CCNode* node,
        CCNode* difficultyFace,
        CCSize const& layerSize,
        int depth,
        std::vector<CCRect>& obstacles
    ) {
        if (!node || node == difficultyFace || !node->isVisible() ||
            depth > kMaximumObstacleDepth) {
            return;
        }

        // Node IDs 1.23.3 exposes this exact vanilla background ID. It is visual,
        // but deliberately not an occupied UI region.
        if (node->getID() == "background") {
            return;
        }

        CCRect rect;
        auto const hasRect = getRectInPauseSpace(pauseLayer, node, rect);
        auto const isMenu = typeinfo_cast<CCMenu*>(node) != nullptr;
        auto const coversScreen = hasRect &&
            rect.size.width >= layerSize.width * 0.85f &&
            rect.size.height >= layerSize.height * 0.85f;

        // Menus and zero/full-size nodes are containers. Their children are the
        // meaningful controls; treating the container itself as an obstacle would
        // incorrectly reserve an entire column or the whole screen.
        if (!isMenu && hasRect && !coversScreen) {
            obstacles.push_back(rect);
            return;
        }

        for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) {
            collectRelevantObstacles(
                pauseLayer,
                child,
                difficultyFace,
                layerSize,
                depth + 1,
                obstacles
            );
        }
    }

    std::vector<CCRect> getRelevantObstacles(
        PauseLayer* pauseLayer,
        CCNode* difficultyFace,
        CCSize const& layerSize
    ) {
        std::vector<CCRect> obstacles;
        for (auto* child : CCArrayExt<CCNode*>(pauseLayer->getChildren())) {
            collectRelevantObstacles(
                pauseLayer,
                child,
                difficultyFace,
                layerSize,
                0,
                obstacles
            );
        }
        return obstacles;
    }

    CCRect faceRectAt(CCNode* face, CCPoint const& position, float scale) {
        auto const contentSize = face->getContentSize();
        auto const width = contentSize.width * scale;
        auto const height = contentSize.height * scale;

        CCRect result;
        result.origin = CCPoint(
            position.x - width * 0.5f,
            position.y - height * 0.5f
        );
        result.size = CCSize(width, height);
        return result;
    }

    CCRect expandedRect(CCRect const& rect, float amount) {
        CCRect result;
        result.origin = CCPoint(rect.origin.x - amount, rect.origin.y - amount);
        result.size = CCSize(
            rect.size.width + amount * 2.f,
            rect.size.height + amount * 2.f
        );
        return result;
    }

    float intersectionArea(CCRect const& first, CCRect const& second) {
        auto const width = std::max(
            0.f,
            std::min(first.origin.x + first.size.width, second.origin.x + second.size.width) -
                std::max(first.origin.x, second.origin.x)
        );
        auto const height = std::max(
            0.f,
            std::min(first.origin.y + first.size.height, second.origin.y + second.size.height) -
                std::max(first.origin.y, second.origin.y)
        );
        return width * height;
    }

    bool rectFitsInside(CCRect const& rect, CCRect const& bounds) {
        return rect.origin.x >= bounds.origin.x &&
            rect.origin.y >= bounds.origin.y &&
            rect.origin.x + rect.size.width <= bounds.origin.x + bounds.size.width &&
            rect.origin.y + rect.size.height <= bounds.origin.y + bounds.size.height;
    }

    bool candidateFits(
        CCNode* face,
        CCPoint const& position,
        float scale,
        CCRect const& safeBounds,
        std::vector<CCRect> const& obstacles
    ) {
        auto const faceRect = faceRectAt(face, position, scale);
        if (!rectFitsInside(faceRect, safeBounds)) {
            return false;
        }

        auto const collisionRect = expandedRect(faceRect, kCollisionMargin);
        return std::none_of(obstacles.begin(), obstacles.end(), [&](CCRect const& obstacle) {
            return intersectionArea(collisionRect, obstacle) > 0.f;
        });
    }

    float candidateOverlap(
        CCNode* face,
        CCPoint const& position,
        float scale,
        std::vector<CCRect> const& obstacles
    ) {
        auto const collisionRect = expandedRect(
            faceRectAt(face, position, scale),
            kCollisionMargin
        );
        float overlap = 0.f;
        for (auto const& obstacle : obstacles) {
            overlap += intersectionArea(collisionRect, obstacle);
        }
        return overlap;
    }

    std::vector<CCPoint> makeCandidates(
        CCPoint const& preferred,
        CCSize const& layerSize,
        CCSize const& faceSize
    ) {
        auto const horizontalStep = faceSize.width * kDifficultyFaceScale + kPositionGap;
        auto const verticalStep = faceSize.height * kDifficultyFaceScale + kPositionGap;
        std::vector<CCPoint> candidates;

        auto addCandidate = [&](CCPoint const& candidate) {
            auto const duplicate = std::any_of(
                candidates.begin(),
                candidates.end(),
                [&](CCPoint const& existing) {
                    return std::abs(existing.x - candidate.x) < 0.5f &&
                        std::abs(existing.y - candidate.y) < 0.5f;
                }
            );
            if (!duplicate) {
                candidates.push_back(candidate);
            }
        };

        // Keep the approved position first. Then try a second left slot, mirror
        // those slots to the right, and finally try the row immediately below.
        addCandidate(preferred);
        addCandidate({preferred.x + horizontalStep, preferred.y});
        addCandidate({layerSize.width - preferred.x, preferred.y});
        addCandidate({layerSize.width - preferred.x - horizontalStep, preferred.y});
        addCandidate({preferred.x, preferred.y - verticalStep});
        addCandidate({layerSize.width - preferred.x, preferred.y - verticalStep});

        return candidates;
    }

    PlacementResult placeDifficultyFace(
        PauseLayer* pauseLayer,
        CCNode* difficultyFace,
        CCPoint const& preferred,
        CCSize const& layerSize
    ) {
        auto const obstacles = getRelevantObstacles(
            pauseLayer,
            difficultyFace,
            layerSize
        );
        auto const candidates = makeCandidates(
            preferred,
            layerSize,
            difficultyFace->getContentSize()
        );

        CCRect safeBounds;
        safeBounds.origin = CCPoint(kSafeInset, kSafeInset);
        safeBounds.size = CCSize(
            std::max(0.f, layerSize.width - kSafeInset * 2.f),
            std::max(0.f, layerSize.height - kSafeInset * 2.f)
        );

        std::array<float, 3> const scales {{
            kDifficultyFaceScale,
            (kDifficultyFaceScale + kMinimumDifficultyFaceScale) * 0.5f,
            kMinimumDifficultyFaceScale,
        }};

        auto const preferredWasFree = candidateFits(
            difficultyFace,
            candidates.front(),
            kDifficultyFaceScale,
            safeBounds,
            obstacles
        );

        for (auto const scale : scales) {
            for (size_t index = 0; index < candidates.size(); ++index) {
                if (candidateFits(
                    difficultyFace,
                    candidates[index],
                    scale,
                    safeBounds,
                    obstacles
                )) {
                    difficultyFace->setScale(scale);
                    difficultyFace->setPosition(candidates[index]);
                    return {
                        candidates[index],
                        scale,
                        index,
                        preferredWasFree,
                        false,
                    };
                }
            }
        }

        // Deterministic last resort: use the in-bounds candidate with the least
        // occupied area at the minimum legible scale. Candidate order breaks ties.
        auto bestIndex = size_t {0};
        auto bestOverlap = std::numeric_limits<float>::max();
        auto foundInBounds = false;
        for (size_t index = 0; index < candidates.size(); ++index) {
            auto const rect = faceRectAt(
                difficultyFace,
                candidates[index],
                kMinimumDifficultyFaceScale
            );
            if (!rectFitsInside(rect, safeBounds)) {
                continue;
            }

            auto const overlap = candidateOverlap(
                difficultyFace,
                candidates[index],
                kMinimumDifficultyFaceScale,
                obstacles
            );
            if (!foundInBounds || overlap < bestOverlap) {
                foundInBounds = true;
                bestIndex = index;
                bestOverlap = overlap;
            }
        }

        auto fallbackPosition = candidates[bestIndex];
        if (!foundInBounds) {
            auto const halfWidth = difficultyFace->getContentSize().width *
                kMinimumDifficultyFaceScale * 0.5f;
            auto const halfHeight = difficultyFace->getContentSize().height *
                kMinimumDifficultyFaceScale * 0.5f;
            auto const minimumX = kSafeInset + halfWidth;
            auto const maximumX = layerSize.width - kSafeInset - halfWidth;
            auto const minimumY = kSafeInset + halfHeight;
            auto const maximumY = layerSize.height - kSafeInset - halfHeight;
            fallbackPosition.x = maximumX >= minimumX
                ? std::clamp(preferred.x, minimumX, maximumX)
                : layerSize.width * 0.5f;
            fallbackPosition.y = maximumY >= minimumY
                ? std::clamp(preferred.y, minimumY, maximumY)
                : layerSize.height * 0.5f;
        }

        difficultyFace->setScale(kMinimumDifficultyFaceScale);
        difficultyFace->setPosition(fallbackPosition);
        return {
            fallbackPosition,
            kMinimumDifficultyFaceScale,
            bestIndex,
            preferredWasFree,
            true,
        };
    }

    void addDifficultyFace(PauseLayer* pauseLayer) {
        if (pauseLayer->getChildByID("difficulty-face"_spr)) {
            log::warn("DF and orbs in Pause Menu: difficulty face already exists; skipping duplicate.");
            return;
        }

        auto* playLayer = PlayLayer::get();
        if (!playLayer || !playLayer->m_level) {
            log::warn("DF and orbs in Pause Menu: no current GJGameLevel was available; difficulty face omitted.");
            return;
        }

        auto* level = playLayer->m_level;
        auto const rawDifficulty = static_cast<int>(level->m_difficulty);
        auto const demonValue = level->m_demon.value();
        auto const difficulty = difficultyForLevel(level);

        log::info(
            "DF and orbs in Pause Menu: current level found (raw difficulty={}, auto={}, demon={}, demon subtype={}); resolved difficulty={} ({}).",
            rawDifficulty,
            level->m_autoLevel,
            demonValue,
            level->m_demonDifficulty,
            difficulty,
            difficultyName(difficulty)
        );

        if (difficulty < static_cast<int>(GJDifficulty::NA) ||
            difficulty > static_cast<int>(GJDifficulty::DemonExtreme)) {
            log::warn("DF and orbs in Pause Menu: resolved difficulty {} is outside GD's supported range; face omitted.", difficulty);
            return;
        }

        auto const frameName = GJDifficultySprite::getDifficultyFrame(
            difficulty,
            GJDifficultyName::Short
        );

        log::info("DF and orbs in Pause Menu: attempting original GD difficulty frame '{}'.", frameName.c_str());

        if (frameName.empty() ||
            !CCSpriteFrameCache::get()->spriteFrameByName(frameName.c_str())) {
            log::warn("DF and orbs in Pause Menu: original GD difficulty frame '{}' was unavailable; face omitted.", frameName.c_str());
            return;
        }

        auto* difficultyFace = GJDifficultySprite::create(
            difficulty,
            GJDifficultyName::Short
        );
        if (!difficultyFace) {
            log::warn("DF and orbs in Pause Menu: GJDifficultySprite creation failed for '{}'; face omitted.", frameName.c_str());
            return;
        }

        auto layerSize = pauseLayer->getContentSize();
        if (layerSize.width <= 0.f || layerSize.height <= 0.f) {
            layerSize = CCDirector::get()->getWinSize();
        }

        auto const faceX = std::clamp(layerSize.width * 0.08f, 36.f, 48.f);
        auto faceY = layerSize.height - 32.f;
        if (auto* levelName = pauseLayer->getChildByID("level-name")) {
            faceY = levelName->getPositionY();
        }
        auto const preferredPosition = CCPoint {faceX, faceY};

        difficultyFace->setID("difficulty-face"_spr);
        difficultyFace->setScale(kDifficultyFaceScale);
        difficultyFace->setAnchorPoint({0.5f, 0.5f});
        difficultyFace->setPosition(preferredPosition);
        pauseLayer->addChild(difficultyFace, 10);

        auto const placement = placeDifficultyFace(
            pauseLayer,
            difficultyFace,
            preferredPosition,
            layerSize
        );

        if (placement.preferredWasFree) {
            log::info(
                "DF and orbs in Pause Menu: Adaptive layout: preferred position free (candidate 1, scale {}).",
                placement.scale
            );
        }
        else {
            log::info("DF and orbs in Pause Menu: Adaptive layout: conflict detected at preferred position.");
            if (placement.usedFallback) {
                log::warn(
                    "DF and orbs in Pause Menu: Adaptive layout: no collision-free candidate; selected least-overlap candidate {} at minimum scale {}.",
                    placement.candidateIndex + 1,
                    placement.scale
                );
            }
            else {
                log::info(
                    "DF and orbs in Pause Menu: Adaptive layout: selected candidate {} with final scale {}.",
                    placement.candidateIndex + 1,
                    placement.scale
                );
            }
        }

        log::info(
            "DF and orbs in Pause Menu: difficulty face created successfully at ({}, {}) with scale {}.",
            placement.position.x,
            placement.position.y,
            placement.scale
        );
    }
}

class $modify(DFPauseLayer, PauseLayer) {
    static void onModify(auto& self) {
        if (!self.setHookPriorityPost("PauseLayer::customSetup", Priority::Last)) {
            log::warn("DF and orbs in Pause Menu: failed to set final post-hook priority; adaptive layout may miss UI added by later hooks.");
        }
    }

    void customSetup() {
        log::info("DF and orbs in Pause Menu: PauseLayer::customSetup hook reached before original.");

        PauseLayer::customSetup();

        addDifficultyFace(this);

        log::info("DF and orbs in Pause Menu: PauseLayer::customSetup hook completed.");
    }
};

$on_mod(Loaded) {
    log::info("DF and orbs in Pause Menu loaded successfully with Geode $modify PauseLayer hook.");
}
