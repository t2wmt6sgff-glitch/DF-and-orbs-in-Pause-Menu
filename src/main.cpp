#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/binding/GameStatsManager.hpp>
#include <Geode/binding/GJDifficultySprite.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/PauseLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace geode::prelude;

namespace {
    // The contents keep fixed, vanilla-like proportions. Adaptive layout moves
    // and, only as a last resort, scales the complete stats block as one unit.
    constexpr float kDifficultyFaceScale = 0.80f;
    constexpr float kOrbIconScale = 0.60f;
    constexpr float kOrbLabelScale = 0.40f;
    constexpr float kOrbItemGap = 3.f;
    constexpr float kStatsPadding = 3.f;
    constexpr float kStatsVerticalGap = 4.f;
    constexpr float kPreferredContainerScale = 1.f;
    constexpr float kMinimumContainerScale = 0.90f;
    constexpr float kScreenInset = 14.f;
    constexpr float kVanillaSafetyPadding = 7.f;
    constexpr float kExternalSafetyPadding = 5.f;
    constexpr float kMinimumObstacleSize = 1.f;
    constexpr int kMaximumObstacleDepth = 7;
    constexpr char const* kOrbSpriteFrame = "currencyOrbIcon_001.png";

    struct LevelStatsData {
        int difficulty;
        std::string difficultyFrame;
        std::optional<int> awardedCurrency;
    };

    struct Obstacle {
        CCRect rect;
        std::string sourceID;
        bool criticalVanilla;
    };

    struct PlacementCandidate {
        char const* name;
        CCPoint position;
    };

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

    std::optional<LevelStatsData> readLevelStats() {
        auto* playLayer = PlayLayer::get();
        if (!playLayer || !playLayer->m_level) {
            log::warn("DF and orbs in Pause Menu: no current GJGameLevel was available; stats block omitted.");
            return std::nullopt;
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
            log::warn("DF and orbs in Pause Menu: resolved difficulty {} is outside GD's supported range; stats block omitted.", difficulty);
            return std::nullopt;
        }

        auto const frameName = GJDifficultySprite::getDifficultyFrame(
            difficulty,
            GJDifficultyName::Short
        );
        log::info("DF and orbs in Pause Menu: attempting original GD difficulty frame '{}'.", frameName.c_str());

        auto* frameCache = CCSpriteFrameCache::get();
        if (frameName.empty() || !frameCache ||
            !frameCache->spriteFrameByName(frameName.c_str())) {
            log::warn("DF and orbs in Pause Menu: original GD difficulty frame '{}' was unavailable; stats block omitted.", frameName.c_str());
            return std::nullopt;
        }

        LevelStatsData data {
            difficulty,
            frameName,
            std::nullopt,
        };

        auto const stars = level->m_stars.value();
        auto const platformer = level->isPlatformer();
        auto const testMode = playLayer->m_isTestMode;
        if (stars <= 0 || platformer || testMode) {
            log::info(
                "DF and orbs in Pause Menu: level ID {} does not use persistent normal-mode Mana Orb rewards (stars={}, platformer={}, test mode={}); reward row omitted.",
                level->m_levelID.value(),
                stars,
                platformer,
                testMode
            );
            return data;
        }

        auto* statsManager = GameStatsManager::get();
        if (!statsManager) {
            log::warn("DF and orbs in Pause Menu: GameStatsManager was unavailable; reward row omitted.");
            return data;
        }

        // Geometry Dash owns the official/main, online, Daily/Weekly and Gauntlet
        // currency-score dictionaries. Reusing its getter avoids global currency
        // and avoids duplicating the game's level-key selection and reward rules.
        auto const baseCurrency = statsManager->getBaseCurrencyForLevel(level);
        if (baseCurrency <= 0) {
            log::info(
                "DF and orbs in Pause Menu: level ID {} has no Mana Orb reward (base currency={}); reward row omitted.",
                level->m_levelID.value(),
                baseCurrency
            );
            return data;
        }

        auto const awardedCurrency = statsManager->getAwardedCurrencyForLevel(level);
        if (awardedCurrency < 0) {
            log::warn(
                "DF and orbs in Pause Menu: Geometry Dash returned invalid awarded currency {} for level ID {}; reward row omitted.",
                awardedCurrency,
                level->m_levelID.value()
            );
            return data;
        }

        data.awardedCurrency = awardedCurrency;
        log::info(
            "DF and orbs in Pause Menu: level ID {} awarded Mana Orbs={} (stars={}, daily ID={}, gauntlet={}, base currency={}).",
            level->m_levelID.value(),
            awardedCurrency,
            stars,
            level->m_dailyID.value(),
            level->m_gauntletLevel,
            baseCurrency
        );
        return data;
    }

    CCNode* createRewardsRow(int awardedCurrency) {
        auto* frameCache = CCSpriteFrameCache::get();
        if (!frameCache || !frameCache->spriteFrameByName(kOrbSpriteFrame)) {
            log::warn(
                "DF and orbs in Pause Menu: original GD Mana Orb frame '{}' was unavailable; reward row omitted.",
                kOrbSpriteFrame
            );
            return nullptr;
        }

        auto* orbSprite = CCSprite::createWithSpriteFrameName(kOrbSpriteFrame);
        auto const orbText = fmt::format("{}", awardedCurrency);
        auto* orbLabel = CCLabelBMFont::create(orbText.c_str(), "bigFont.fnt");
        auto* rewardsRow = CCNode::create();
        if (!orbSprite || !orbLabel || !rewardsRow) {
            log::warn("DF and orbs in Pause Menu: Mana Orb UI creation failed; reward row omitted.");
            return nullptr;
        }

        orbSprite->setID("orb-sprite"_spr);
        orbSprite->setScale(kOrbIconScale);
        orbSprite->setAnchorPoint({0.f, 0.5f});

        orbLabel->setID("orb-label"_spr);
        orbLabel->setScale(kOrbLabelScale);
        orbLabel->setAnchorPoint({0.f, 0.5f});

        auto const iconWidth = orbSprite->getContentSize().width * kOrbIconScale;
        auto const iconHeight = orbSprite->getContentSize().height * kOrbIconScale;
        auto const labelWidth = orbLabel->getContentSize().width * kOrbLabelScale;
        auto const labelHeight = orbLabel->getContentSize().height * kOrbLabelScale;
        auto const rowSize = CCSize(
            iconWidth + kOrbItemGap + labelWidth,
            std::max(iconHeight, labelHeight)
        );

        rewardsRow->setID("rewards-row"_spr);
        rewardsRow->setContentSize(rowSize);
        rewardsRow->setAnchorPoint({0.5f, 0.5f});
        rewardsRow->ignoreAnchorPointForPosition(false);

        orbSprite->setPosition({0.f, rowSize.height * 0.5f});
        // A tiny optical correction matches GD's icon/number baselines better
        // than strict mathematical centering with bigFont.fnt.
        orbLabel->setPosition({iconWidth + kOrbItemGap, rowSize.height * 0.5f + 0.5f});
        rewardsRow->addChild(orbSprite);
        rewardsRow->addChild(orbLabel);
        return rewardsRow;
    }

    CCNode* createStatsContainer(LevelStatsData const& data) {
        auto* difficultyFace = GJDifficultySprite::create(
            data.difficulty,
            GJDifficultyName::Short
        );
        auto* statsContainer = CCNode::create();
        if (!difficultyFace || !statsContainer) {
            log::warn("DF and orbs in Pause Menu: stats UI creation failed; block omitted.");
            return nullptr;
        }

        difficultyFace->setID("difficulty-face"_spr);
        difficultyFace->setScale(kDifficultyFaceScale);
        difficultyFace->setAnchorPoint({0.5f, 0.5f});

        CCNode* rewardsRow = nullptr;
        if (data.awardedCurrency) {
            rewardsRow = createRewardsRow(*data.awardedCurrency);
        }

        auto const faceWidth = difficultyFace->getContentSize().width * kDifficultyFaceScale;
        auto const faceHeight = difficultyFace->getContentSize().height * kDifficultyFaceScale;
        auto const rowWidth = rewardsRow ? rewardsRow->getContentSize().width : 0.f;
        auto const rowHeight = rewardsRow ? rewardsRow->getContentSize().height : 0.f;
        auto const contentsHeight = faceHeight +
            (rewardsRow ? kStatsVerticalGap + rowHeight : 0.f);
        auto const containerSize = CCSize(
            std::max(faceWidth, rowWidth) + kStatsPadding * 2.f,
            contentsHeight + kStatsPadding * 2.f
        );

        statsContainer->setID("stats-container"_spr);
        statsContainer->setContentSize(containerSize);
        statsContainer->setAnchorPoint({0.5f, 0.5f});
        statsContainer->ignoreAnchorPointForPosition(false);

        auto const centerX = containerSize.width * 0.5f;
        auto const top = containerSize.height - kStatsPadding;
        difficultyFace->setPosition({centerX, top - faceHeight * 0.5f});
        statsContainer->addChild(difficultyFace);

        if (rewardsRow) {
            rewardsRow->setPosition({centerX, kStatsPadding + rowHeight * 0.5f});
            statsContainer->addChild(rewardsRow);
        }

        return statsContainer;
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

    CCRect expandedRect(CCRect const& rect, float amount) {
        return CCRect(
            rect.origin.x - amount,
            rect.origin.y - amount,
            rect.size.width + amount * 2.f,
            rect.size.height + amount * 2.f
        );
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

    bool isCriticalVanillaID(std::string_view id) {
        // Exact PauseLayer IDs from Geode Node IDs 1.23.3. The audio controls
        // are direct PauseLayer children; center-button-menu reserves the main
        // control cluster as a single hard boundary.
        constexpr std::array<std::string_view, 18> criticalIDs {{
            "level-name",
            "normal-progress-bar",
            "practice-progress-bar",
            "normal-progress-label",
            "practice-progress-label",
            "normal-mode-label",
            "practice-mode-label",
            "center-button-menu",
            "practice-arrow-text",
            "music-slider",
            "sfx-slider",
            "music-label",
            "sfx-label",
            "play-time",
            "points-label",
            "controller-play-hint",
            "controller-back-hint",
            "controller-practice-hint",
        }};
        return std::find(criticalIDs.begin(), criticalIDs.end(), id) != criticalIDs.end();
    }

    void collectRelevantObstacles(
        PauseLayer* pauseLayer,
        CCNode* node,
        CCNode* statsContainer,
        CCSize const& layerSize,
        int depth,
        bool inheritedCritical,
        std::string const& inheritedSourceID,
        std::vector<Obstacle>& obstacles
    ) {
        if (!node || node == statsContainer || !node->isVisible() ||
            depth > kMaximumObstacleDepth) {
            return;
        }

        auto const nodeID = std::string(node->getID());
        if (nodeID == "background") {
            return;
        }

        auto const critical = inheritedCritical || isCriticalVanillaID(nodeID);
        auto const sourceID = nodeID.empty() ? inheritedSourceID : nodeID;
        CCRect rect;
        auto const hasRect = getRectInPauseSpace(pauseLayer, node, rect);
        auto const coversScreen = hasRect &&
            rect.size.width >= layerSize.width * 0.85f &&
            rect.size.height >= layerSize.height * 0.85f;
        auto const isMenu = typeinfo_cast<CCMenu*>(node) != nullptr;

        // Critical semantic nodes (especially SFX/Music and the center menu) are
        // hard boundaries. Non-critical menus are traversed so buttons injected
        // by unknown mods remain discoverable without reserving whole columns.
        if (critical && hasRect && !coversScreen) {
            obstacles.push_back({
                expandedRect(rect, kVanillaSafetyPadding),
                sourceID,
                true,
            });
            return;
        }

        if (coversScreen || isMenu || !hasRect) {
            for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) {
                collectRelevantObstacles(
                    pauseLayer,
                    child,
                    statsContainer,
                    layerSize,
                    depth + 1,
                    critical,
                    sourceID,
                    obstacles
                );
            }
            return;
        }

        auto const hasNamespacedID = nodeID.find('/') != std::string::npos;
        auto const isVisualLeaf = typeinfo_cast<CCSprite*>(node) ||
            typeinfo_cast<CCLabelBMFont*>(node) ||
            typeinfo_cast<CCMenuItem*>(node) ||
            node->getChildrenCount() == 0;

        // A namespaced non-menu node is a useful generic boundary for another
        // mod's UI group. Otherwise collect visual leaves and recurse through
        // anonymous vanilla/decorative containers.
        if (hasNamespacedID || isVisualLeaf) {
            obstacles.push_back({
                expandedRect(rect, kExternalSafetyPadding),
                sourceID,
                false,
            });
            return;
        }

        for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) {
            collectRelevantObstacles(
                pauseLayer,
                child,
                statsContainer,
                layerSize,
                depth + 1,
                false,
                sourceID,
                obstacles
            );
        }
    }

    std::vector<Obstacle> getRelevantObstacles(
        PauseLayer* pauseLayer,
        CCNode* statsContainer,
        CCSize const& layerSize
    ) {
        std::vector<Obstacle> obstacles;
        for (auto* child : CCArrayExt<CCNode*>(pauseLayer->getChildren())) {
            collectRelevantObstacles(
                pauseLayer,
                child,
                statsContainer,
                layerSize,
                0,
                false,
                "",
                obstacles
            );
        }
        return obstacles;
    }

    CCRect containerRectAt(CCNode* container, CCPoint const& position, float scale) {
        auto const size = container->getContentSize();
        auto const width = size.width * scale;
        auto const height = size.height * scale;
        return CCRect(
            position.x - width * 0.5f,
            position.y - height * 0.5f,
            width,
            height
        );
    }

    std::vector<PlacementCandidate> makeCandidates(
        CCNode* statsContainer,
        CCSize const& layerSize
    ) {
        auto const halfWidth = statsContainer->getContentSize().width * 0.5f;
        auto const halfHeight = statsContainer->getContentSize().height * 0.5f;
        auto const leftX = kScreenInset + halfWidth;
        auto const rightX = layerSize.width - kScreenInset - halfWidth;
        auto const topY = layerSize.height - kScreenInset - halfHeight;
        auto const middleY = layerSize.height * 0.61f;
        auto const lowerY = layerSize.height * 0.35f;

        // These are named visual regions, not incremental nudges. The top-left
        // position is preferred; each side is exhausted at full size before the
        // entire block is allowed to shrink.
        return {
            {"top-left", CCPoint(leftX, topY)},
            {"top-right", CCPoint(rightX, topY)},
            {"middle-left", CCPoint(leftX, middleY)},
            {"middle-right", CCPoint(rightX, middleY)},
            {"lower-left", CCPoint(leftX, lowerY)},
            {"lower-right", CCPoint(rightX, lowerY)},
        };
    }

    bool candidateFits(
        CCNode* statsContainer,
        PlacementCandidate const& candidate,
        float scale,
        CCRect const& safeBounds,
        std::vector<Obstacle> const& obstacles,
        bool criticalOnly = false
    ) {
        auto const rect = containerRectAt(statsContainer, candidate.position, scale);
        if (!rectFitsInside(rect, safeBounds)) {
            return false;
        }

        return std::none_of(obstacles.begin(), obstacles.end(), [&](Obstacle const& obstacle) {
            return (!criticalOnly || obstacle.criticalVanilla) &&
                intersectionArea(rect, obstacle.rect) > 0.f;
        });
    }

    std::string candidateBlocker(
        CCNode* statsContainer,
        PlacementCandidate const& candidate,
        float scale,
        std::vector<Obstacle> const& obstacles
    ) {
        auto const rect = containerRectAt(statsContainer, candidate.position, scale);
        auto blocker = std::find_if(obstacles.begin(), obstacles.end(), [&](Obstacle const& obstacle) {
            return obstacle.criticalVanilla && intersectionArea(rect, obstacle.rect) > 0.f;
        });
        if (blocker == obstacles.end()) {
            blocker = std::find_if(obstacles.begin(), obstacles.end(), [&](Obstacle const& obstacle) {
                return intersectionArea(rect, obstacle.rect) > 0.f;
            });
        }
        return blocker == obstacles.end() || blocker->sourceID.empty()
            ? "unidentified UI"
            : blocker->sourceID;
    }

    float externalOverlap(
        CCNode* statsContainer,
        PlacementCandidate const& candidate,
        float scale,
        std::vector<Obstacle> const& obstacles
    ) {
        auto const rect = containerRectAt(statsContainer, candidate.position, scale);
        float overlap = 0.f;
        for (auto const& obstacle : obstacles) {
            if (!obstacle.criticalVanilla) {
                overlap += intersectionArea(rect, obstacle.rect);
            }
        }
        return overlap;
    }

    std::optional<PlacementResult> placeStatsContainer(
        PauseLayer* pauseLayer,
        CCNode* statsContainer,
        CCSize const& layerSize
    ) {
        auto const obstacles = getRelevantObstacles(pauseLayer, statsContainer, layerSize);
        auto const candidates = makeCandidates(statsContainer, layerSize);
        auto const safeBounds = CCRect(
            kScreenInset,
            kScreenInset,
            std::max(0.f, layerSize.width - kScreenInset * 2.f),
            std::max(0.f, layerSize.height - kScreenInset * 2.f)
        );

        auto const preferredWasFree = candidateFits(
            statsContainer,
            candidates.front(),
            kPreferredContainerScale,
            safeBounds,
            obstacles
        );
        if (!preferredWasFree) {
            log::info(
                "DF and orbs in Pause Menu: Pause UI layout: preferred region blocked by '{}'.",
                candidateBlocker(
                    statsContainer,
                    candidates.front(),
                    kPreferredContainerScale,
                    obstacles
                )
            );
        }

        // Move before shrinking: every designed region is tested at full size,
        // followed by the same ordered regions at the minimum readable scale.
        constexpr std::array<float, 2> scales {{
            kPreferredContainerScale,
            kMinimumContainerScale,
        }};
        for (auto const scale : scales) {
            for (size_t index = 0; index < candidates.size(); ++index) {
                if (candidateFits(
                    statsContainer,
                    candidates[index],
                    scale,
                    safeBounds,
                    obstacles
                )) {
                    statsContainer->setScale(scale);
                    statsContainer->setPosition(candidates[index].position);
                    return PlacementResult {
                        candidates[index].position,
                        scale,
                        index,
                        preferredWasFree,
                        false,
                    };
                }
            }
        }

        // Last resort: never cover vanilla controls. Among critical-safe regions,
        // choose the smallest overlap with external UI; visual candidate order is
        // the deterministic tie-breaker. Omit only if every region is unsafe.
        auto bestIndex = size_t {0};
        auto bestOverlap = std::numeric_limits<float>::max();
        auto foundCriticalSafe = false;
        for (size_t index = 0; index < candidates.size(); ++index) {
            if (!candidateFits(
                statsContainer,
                candidates[index],
                kMinimumContainerScale,
                safeBounds,
                obstacles,
                true
            )) {
                continue;
            }

            auto const overlap = externalOverlap(
                statsContainer,
                candidates[index],
                kMinimumContainerScale,
                obstacles
            );
            if (!foundCriticalSafe || overlap < bestOverlap) {
                foundCriticalSafe = true;
                bestIndex = index;
                bestOverlap = overlap;
            }
        }

        if (!foundCriticalSafe) {
            log::warn("DF and orbs in Pause Menu: Pause UI layout: no region was clear of critical vanilla controls; stats block omitted.");
            return std::nullopt;
        }

        statsContainer->setScale(kMinimumContainerScale);
        statsContainer->setPosition(candidates[bestIndex].position);
        return PlacementResult {
            candidates[bestIndex].position,
            kMinimumContainerScale,
            bestIndex,
            preferredWasFree,
            true,
        };
    }

    void addStatsBlock(PauseLayer* pauseLayer) {
        if (!pauseLayer) {
            log::warn("DF and orbs in Pause Menu: PauseLayer was unavailable; stats block omitted.");
            return;
        }

        if (pauseLayer->getChildByID("stats-container"_spr)) {
            log::warn("DF and orbs in Pause Menu: stats block already exists; skipping duplicate.");
            return;
        }

        auto const data = readLevelStats();
        if (!data) {
            return;
        }

        auto* statsContainer = createStatsContainer(*data);
        if (!statsContainer) {
            return;
        }

        auto layerSize = pauseLayer->getContentSize();
        if (layerSize.width <= 0.f || layerSize.height <= 0.f) {
            layerSize = CCDirector::get()->getWinSize();
        }

        // Keep the incomplete placement invisible, then reveal it at its final
        // position. There is no deferred frame and therefore no visible jump.
        statsContainer->setVisible(false);
        pauseLayer->addChild(statsContainer, 10);
        auto const placement = placeStatsContainer(pauseLayer, statsContainer, layerSize);
        if (!placement) {
            statsContainer->removeFromParentAndCleanup(true);
            return;
        }

        statsContainer->setVisible(true);
        auto const candidates = makeCandidates(statsContainer, layerSize);
        log::info(
            "DF and orbs in Pause Menu: Pause UI layout: selected {} (candidate {}), final position=({}, {}), scale={}{}.",
            candidates[placement->candidateIndex].name,
            placement->candidateIndex + 1,
            placement->position.x,
            placement->position.y,
            placement->scale,
            placement->usedFallback ? ", critical-safe fallback" : ""
        );
        log::info(
            "DF and orbs in Pause Menu: stats block created successfully with difficulty frame '{}'{}.",
            data->difficultyFrame,
            data->awardedCurrency ? " and per-level Mana Orbs" : ""
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

        addStatsBlock(this);

        log::info("DF and orbs in Pause Menu: PauseLayer::customSetup hook completed.");
    }
};

$on_mod(Loaded) {
    log::info("DF and orbs in Pause Menu loaded successfully with Geode $modify PauseLayer hook.");
}
