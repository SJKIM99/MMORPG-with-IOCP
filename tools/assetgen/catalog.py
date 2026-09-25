# -*- coding: utf-8 -*-
"""
하르벤 게임 에셋 목록 — Higgsfield(GPT Image 2.5)로 만든 모든 그림의 "주문서".

· 한 줄 = 한 번의 생성(대부분 1024px, 2x2 격자 한 장에 동작 4개).
· 모든 프롬프트는 같은 스타일 문구(STYLE_*)로 끝난다 — 캐릭터 · 소품 · 아이콘 · 효과가
  한 게임처럼 보이게 하는 장치다. 스타일을 바꾸려면 여기만 고친다(docs/DESIGN_SYSTEM.md 9장).
· 생성 결과(job id · URL)는 jobs.json, 가공은 process.py.
"""

# ─────────────────────────────────────────────────────────────────────────────
# 스타일
# ─────────────────────────────────────────────────────────────────────────────
STYLE_SPRITE = (
    "Style: hand-painted digital painting for a premium 2.5D isometric dark-fantasy MMORPG, "
    "crisp readable silhouette, subtle dark ink outline, soft warm key light from the top-left and a faint cool teal rim light, "
    "restrained natural palette (deep slate blue, teal, muted amber, earthy greens and browns), grounded believable anatomy, "
    "no chibi proportions. Transparent background, no ground, no cast shadow, no text, no border, no grid lines, no motion lines."
)
STYLE_PROP = (
    "Style: hand-painted digital painting for a premium 2.5D isometric dark-fantasy MMORPG, crisp readable silhouette, "
    "subtle dark ink outline, soft warm key light from the top-left, restrained natural palette, "
    "slightly stylized but believable. Transparent background, no ground plane, no cast shadow, no text, no border, no grid lines."
)
STYLE_TEX = (
    "Seamless tileable texture, viewed straight down from above (orthographic top-down), even flat lighting with no directional shadows, "
    "no perspective, no horizon, no objects larger than a hand, uniform even distribution of detail across the whole image with no focal point, "
    "medium-low contrast, hand-painted game texture with fine painterly detail, "
    "muted natural colors for a dark-fantasy MMORPG, fills the entire square edge to edge, no border, no text."
)
STYLE_ICON = (
    "Style: premium dark-fantasy MMORPG inventory icon art, hand-painted, strong readable silhouette, subtle dark outline, "
    "warm key light from the top-left, rich but restrained colors, each object centered in its cell and filling about 80 percent of it, "
    "transparent background, no frames, no borders, no text, no numbers, no grid lines."
)
STYLE_FX = (
    "Game visual-effect sprite sheet on a pure black background (#000000), bright glowing effects only, soft painterly energy, "
    "no characters, no text, no border, no grid lines, each element centered in its own cell with empty black margin."
)

ISO = "isometric three-quarter view from above (about 35 degrees)"

LAYOUT_4 = (
    "Layout: a 2x2 grid showing the SAME {what} four times, one pose centered in each quadrant, identical design, colors and size in every pose, "
    + ISO + ", {facing}, feet or base resting on the same baseline near the bottom of each quadrant, "
    "generous empty margin so nothing touches another quadrant or the image edge. "
)
FACE_SE = "body facing toward the bottom-right"
FACE_NE = "body facing toward the top-right with the back to the viewer (walking away from the camera)"

# ─────────────────────────────────────────────────────────────────────────────
# 몬스터(34) — 원형(archetype)마다 "움직임 · 공격" 동작 말
# ─────────────────────────────────────────────────────────────────────────────
MOVE = {
    "blob": ("squashed mid-hop, body stretched", "slamming forward with its whole body stretched into a lunge"),
    "quad": ("mid-gallop stride, legs extended", "leaping bite with jaws wide open"),
    "humanoid": ("walking mid-stride", "swinging its weapon in a powerful slash toward the bottom-right"),
    "insect": ("legs mid-scuttle", "striking forward with its mandibles"),
    "scorpion": ("legs mid-scuttle, tail raised", "stabbing its tail stinger forward over its head"),
    "golem": ("heavy lumbering step", "smashing down with both huge fists"),
    "ghost": ("gliding forward, trailing wisps", "lashing out with spectral claws"),
    "dragon": ("stalking forward, wings half open", "lunging bite with wings flared"),
    "demon": ("prowling stride", "cleaving down with its weapon"),
    "crab": ("sidestepping with claws raised", "snapping a huge claw forward"),
    "mushroom": ("waddling mid-step", "head-butting forward with its cap, spores puffing"),
    "skeleton": ("marching mid-stride", "slashing with its sword"),
    "zombie": ("walking mid-stride", "clawing forward with both hands toward the bottom-right"),
}

MONSTERS = [
    # id, key, archetype, description
    (1, "sap_blob", "blob", "a small translucent blob of glossy green tree sap with leaves and twigs suspended inside it and two dark bead-like eyes, eerie but a little cute"),
    (2, "grak_raider", "humanoid", "a short wiry Grak raider (tusked orc-kin tribesman) with mossy grey-green skin, patched leather rags and a bone necklace, holding a crude cleaver and a small round wooden shield"),
    (3, "wild_dog", "quad", "a scrawny feral wild dog with matted tan-and-brown fur, bared teeth and a notched ear"),
    (4, "redcap", "mushroom", "a walking mushroom creature with a big dome-shaped red cap spotted with cream, a stubby pale stem body with two short legs and an angry little face under the cap"),
    (5, "grey_wolf", "quad", "a lean grey wolf with a darker back, snarling"),
    (6, "brown_bear", "quad", "a massive brown bear on all fours with shaggy fur and heavy claws"),
    (7, "dew_blob", "blob", "a translucent pale-blue water blob with a glistening surface and a bright dewdrop core floating inside"),
    (8, "grak_spearman", "humanoid", "a Grak spearman (tusked orc-kin) with green skin in rust-orange leather and fur, holding a long crude spear with a bone blade"),
    (9, "sand_scorpion", "scorpion", "a large sand-colored desert scorpion with a raised segmented tail and pale amber pincers"),
    (10, "armored_crab", "crab", "a big armored shore crab with a spiky red-orange shell and huge uneven claws"),
    (11, "leaf_spirit", "ghost", "a floating forest spirit made of glowing pale-green leaves swirling into a slender humanoid wisp with two soft white eyes"),
    (12, "grak_warrior", "humanoid", "a hulking Grak warrior (tusked orc-kin) with dark green skin in crude riveted iron plates and a horned iron helmet, wielding a heavy jagged war axe"),
    (13, "bone_sentry", "skeleton", "a skeleton sentry in rusted remnants of chainmail and a dented helm, holding a notched sword and a cracked round shield"),
    (14, "frost_wolf", "quad", "a large white frost wolf with pale ice-blue tinted fur, frost crystals on its mane and misty breath"),
    (15, "ice_stoneborn", "golem", "a hulking stoneborn (living stone golem) of blue-white ice and frozen rock with jagged crystal shards on its shoulders"),
    (16, "mud_blob", "blob", "a large blob of dark swamp mud with bubbles, reeds and tangled roots sticking out, two small glowing yellow eyes"),
    (17, "burrow_ant", "insect", "a giant dark-red burrowing ant with a glossy carapace and large mandibles"),
    (18, "wraith", "ghost", "a hooded translucent pale-violet wraith with long tattered robes trailing into mist and faint glowing eyes, carrying an old bell-ringer's lantern"),
    (19, "ancient_stoneborn", "golem", "an ancient stoneborn golem built from weathered grey granite blocks carved with faint glowing teal glyphs, patches of moss"),
    (20, "walking_dead", "zombie", "a shambling undead villager with grey-green withered skin in tattered peasant clothes, arms reaching forward, not gory"),
    (21, "oathbreaker", "skeleton", "an undead knight in blackened, dented plate armor with a torn dark-blue surcoat, a faint pale-blue light glowing inside the visor, holding a broken longsword"),
    (22, "shade_spider", "insect", "a large shadowy spider with a purple-black carapace, eight long legs and violet glowing markings"),
    (23, "crimson_scorpion", "scorpion", "a large crimson scorpion with a glossy blood-red shell and a black stinger"),
    (24, "young_wyvern", "dragon", "a young wyvern with orange scales, leathery wings and a long tail, the size of a horse"),
    (25, "wyvern", "dragon", "an adult wyvern with dark crimson scales, spiked tail and tattered leathery wings"),
    (26, "lava_stoneborn", "golem", "a stoneborn golem of black volcanic rock split by glowing molten orange cracks, dripping embers"),
    (27, "kaldun_hound", "quad", "a hellish hound from the Kaldun rift with charcoal-black hide split by glowing ember-orange cracks and burning eyes"),
    (28, "kaldun_warrior", "demon", "a horned warrior from the Kaldun rift, tall and gaunt, crimson-black armored hide, carrying a curved serrated blade, embers drifting from its shoulders"),
    # 보스 — 더 크고 위엄 있게(스프라이트는 같은 크기로 그리고 게임에서 키운다)
    (29, "moroz", "golem", "BOSS: Moroz, the Bitter Frost of Belgora — a colossal frost titan of ice and snow-covered stone with a crown of icicles, a long frozen beard of ice and a glowing pale-blue heart-stone in its chest"),
    (30, "urgan", "humanoid", "BOSS: Urgan, great warchief of the Grak — a huge tusked orc chieftain in heavy iron-and-bone armor with a trophy-laden fur cloak and war paint, wielding a massive two-handed cleaver"),
    (31, "severin", "skeleton", "BOSS: Severin, the fallen knight-commander — a tall undead knight in ornate blackened plate armor with a tattered crimson-and-gold order surcoat, a smoldering ember-orange light inside the helm, a great sword wreathed in dark flame"),
    (32, "burrow_queen", "insect", "BOSS: the Burrow Queen — a monstrous ant queen with a swollen glowing amber abdomen, dark red armored carapace and crown-like mandibles"),
    (33, "zmaj", "dragon", "BOSS: Zmaj, the old dragon of Batra — an ancient fire dragon with massive curling horns, orange-red scales, a golden underbelly and embers falling from its jaws"),
    (34, "gramok", "demon", "BOSS: Gramok the Devourer — a colossal hunched abyssal demon with a gaping fanged maw in its chest glowing with swallowed fire, black-crimson hide and great curved horns"),
]


def monster_prompt(arch, desc):
    move, attack = MOVE[arch]
    what = "creature"
    return (
        f"Animation pose sheet for a monster in an isometric 2.5D dark-fantasy MMORPG: {desc}. "
        + LAYOUT_4.format(what=what, facing=FACE_SE)
        + f"Top-left: idle, alert and breathing. Top-right: moving ({move}). "
        f"Bottom-left: attacking ({attack}), at full extension toward the bottom-right. "
        "Bottom-right: hurt, recoiling backward and flinching from a hit. "
        + STYLE_SPRITE
    )


# ─────────────────────────────────────────────────────────────────────────────
# 플레이어 직업(3) — 기본(서 있는 모습, 이미 생성) + 동작판 3장
# ─────────────────────────────────────────────────────────────────────────────
PLAYER_BASE_JOBS = {
    "knight": "c8d36895-8b3c-4c20-8a7e-9551a02dac5d",
    "engraver": "42a942b8-82c7-44ce-90d7-b52bf700d1c5",
    "hunter": "f1f8b7f3-753c-40fc-922f-f964cf02002a",
}
PLAYER_KEEP = {
    "knight": "identical armor, blue surcoat, cape, helmet, round shield with the pale-gold star emblem, longsword",
    "engraver": "identical hooded slate-blue robe, belts, satchels, brass scroll case, tall carved staff with the teal-glowing engraved stone",
    "hunter": "identical charcoal leather armor, crimson scarf over the lower face, short hood, twin curved daggers",
}
PLAYER_POSES = {
    # (sheet, [TL, TR, BL, BR], facing)
    "knight": {
        "act": ["walking, left leg striding forward, sword arm swinging back", "walking, right leg striding forward, sword arm swinging forward",
                "attack wind-up, body coiled and leaning back, sword raised high behind the shoulder, shield forward",
                "attack strike, lunging forward, sword swung down and across in a powerful diagonal slash, arm fully extended"],
        "cmb": ["charging a skill: sword raised high overhead in both hands, legs braced wide",
                "guarding: shield raised firmly in front, crouched low and braced",
                "hurt: recoiling backward from a heavy hit, head snapped back, flinching",
                "defeated: collapsed lying on the ground on its side, sword dropped beside it"],
        "back": ["standing idle", "walking away, left leg forward", "walking away, right leg forward",
                 "attack strike toward the top-right, sword swung in a wide slash"],
    },
    "engraver": {
        "act": ["walking, left leg forward, staff planted beside the step", "walking, right leg forward, staff swinging forward",
                "casting wind-up: staff drawn back, the engraved stone gathering bright teal light",
                "casting release: staff thrust forward toward the bottom-right, free hand sweeping out, robe flaring"],
        "cmb": ["channeling a skill: staff raised high, the stone blazing teal, free hand tracing a glyph in the air",
                "guarding: staff planted, crouched, free hand raised defensively",
                "hurt: recoiling backward from a heavy hit, flinching",
                "defeated: collapsed lying on the ground on its side, staff dropped beside it"],
        "back": ["standing idle", "walking away, left leg forward", "walking away, right leg forward",
                 "casting toward the top-right, staff thrust forward"],
    },
    "hunter": {
        "act": ["running lightly, left leg forward, daggers held low", "running lightly, right leg forward, daggers held low",
                "attack wind-up: coiled low, both daggers drawn back ready to strike",
                "attack strike: lunging forward in a fast double stab, both arms extended toward the bottom-right"],
        "cmb": ["preparing a skill: crouched very low with both daggers crossed, ready to spring",
                "evading: leaping backward in a quick evasive dodge, body low",
                "hurt: recoiling backward from a heavy hit, flinching",
                "defeated: collapsed lying on the ground on its side, daggers dropped beside it"],
        "back": ["standing idle seen from behind, crouched ready, back of the hood visible",
                 "running away from the viewer toward the top-right, left leg forward, back turned",
                 "running away from the viewer toward the top-right, right leg forward, back turned",
                 "attack strike toward the top-right seen from behind, daggers lashing out, back turned"],
    },
}


def player_prompt(cls, sheet):
    p = PLAYER_POSES[cls][sheet]
    facing = FACE_NE if sheet == "back" else FACE_SE
    behind = ("Every pose is seen FROM BEHIND: the back of the head or hood and the back of the body are visible, the face is NOT visible. "
              if sheet == "back" else "")
    return (
        f"Animation pose sheet of the exact same character as the reference image ({PLAYER_KEEP[cls]}, same colors, proportions and hand-painted style). "
        + behind
        + LAYOUT_4.format(what="character", facing=facing)
        + f"Top-left: {p[0]}. Top-right: {p[1]}. Bottom-left: {p[2]}. Bottom-right: {p[3]}. "
        + STYLE_SPRITE
    )


# ─────────────────────────────────────────────────────────────────────────────
# NPC — 사람은 초상화를 참고 그림으로(얼굴 · 옷 · 색이 대화창과 같게)
# ─────────────────────────────────────────────────────────────────────────────
PORTRAIT_JOBS = {
    1: "a9613ee4-a21f-4215-8c57-b12df5c1fddd",
    2: "aaac71a2-1b8e-47a2-a477-979636e71c95",
    3: "f7bde727-7ab6-4218-87b7-d836bdd2508d",
    4: "0f12db50-f035-4824-bb6f-512f5480b394",
    5: "d6c04032-b95b-4e34-b786-b435d42c3e09",   # 경비병 공용
    6: "461f4e03-aa9e-448f-adb1-6a0d3d758337",
    7: "f9664fc5-f440-4155-b9a8-291517ba7fc6",
    8: "7358ce21-13c7-49b4-87e1-9fb0de14e2c5",
    9: "1e06bb6a-18c1-4c10-afe3-2566e4289f6e",
    10: "91fa01b9-cc3a-43a4-829f-57dc0d05adb9",
    11: "328c86e7-ebab-438d-b51f-2fe3786696ed",
    12: "dd2831c8-ed0e-45ab-bb77-ddde357b314e",
    13: "100e62d5-9b9a-4f1c-a076-a8936857cca4",
}
NPCS = [
    # sprite key, portrait id, full-body description (for pose continuity)
    ("npc_lada", 1, "the village general-goods merchant woman: mustard headscarf over braided hair, deep red dress with a cream apron, carrying a small wicker basket of potion bottles"),
    ("npc_dobran", 2, "the burly bald blacksmith with a red beard, rolled sleeves, a scorched leather apron and a heavy forging hammer"),
    ("npc_mateo", 3, "the grey-haired guide in a long slate-blue hooded robe with brass trim, a leather scroll case on his back and a tall staff topped with a teal-glowing carved stone"),
    ("npc_yaro", 4, "the watch captain: open steel helmet with a red plume, chainmail, a blue tabard with a small white tower emblem, holding a tall spear"),
    ("npc_guard", 5, "a young town guard: open steel helmet with a red plume, chainmail, a blue tabard, a tall spear and a round blue shield"),
    ("npc_goran", 6, "the white-bearded village elder in a long dark-green robe with a thick fur collar, leaning on a gnarled walking staff"),
    ("npc_milena", 7, "the young priestess in a long cream robe and white veil with a small bronze bell pendant, holding a lit candle"),
    ("npc_vesna", 8, "the plump innkeeper with a red headscarf, brown dress with a white apron, holding a big wooden ladle"),
    ("npc_bojan", 9, "the wandering bard with a feathered hat, a dusky purple cloak over a blue tunic, carrying a lute"),
    ("npc_misha", 10, "a cheerful village boy of about nine with messy brown hair in a simple blue linen shirt and brown trousers"),
    ("npc_anka", 11, "a little village girl of about eight with long red braids in a rose-pink dress with a lace collar"),
    ("npc_marfa", 12, "an old village woman with grey hair in a purple knitted shawl, carrying a wicker basket of red apples"),
    ("npc_danilo", 13, "a weathered farmer with a mustache and a wide straw hat, a linen shirt and ochre overalls, carrying a wooden pitchfork"),
]


def npc_prompt(desc):
    return (
        f"Full-body animation pose sheet of the exact same person as in the reference portrait (same face, hair, clothing and colors): {desc}. "
        + LAYOUT_4.format(what="person", facing=FACE_SE)
        + "Top-left: standing idle, relaxed. Top-right: walking, left leg forward. Bottom-left: walking, right leg forward. "
        "Bottom-right: talking, gesturing with one open hand. "
        + STYLE_SPRITE
    )


ANIMALS = [
    ("npc_dog", "a friendly scruffy yellow-tan village dog with floppy ears",
     ["sitting, tail wagging", "trotting, left legs forward", "trotting, right legs forward", "jumping up happily, front paws raised"]),
    ("npc_cat", "a tabby-and-white village cat with a long tail",
     ["sitting upright, tail curled", "walking, left legs forward", "walking, right legs forward", "stretching lazily"]),
    ("npc_chicken", "a brown village hen with a small red comb",
     ["standing", "walking, one foot raised", "walking, other foot raised", "pecking at the ground"]),
]


def animal_prompt(desc, poses):
    return (
        f"Animation pose sheet for a village animal in an isometric 2.5D dark-fantasy MMORPG: {desc}. "
        + LAYOUT_4.format(what="animal", facing=FACE_SE)
        + f"Top-left: {poses[0]}. Top-right: {poses[1]}. Bottom-left: {poses[2]}. Bottom-right: {poses[3]}. "
        + STYLE_SPRITE
    )


# ─────────────────────────────────────────────────────────────────────────────
# 지형 텍스처(바이옴 15 + 마을 5)
# ─────────────────────────────────────────────────────────────────────────────
TERRAIN = [
    ("ground_meadow", "lush meadow grass with short blades, tiny clover and a few small wildflowers (cream, yellow, soft pink), gentle color variation"),
    ("ground_forest", "dark forest floor of moss, fallen leaves, pine needles and small twigs over damp soil"),
    ("ground_snow", "wind-packed snow with soft drifts, faint footprints of small animals and a few blue shadows in hollows"),
    ("ground_canyon", "red sandstone canyon ground with layered strata, cracks and small loose pebbles"),
    ("ground_volcanic", "black volcanic basalt ground with a network of thin cracks glowing with molten orange light, ash dust"),
    ("ground_fairy", "enchanted forest floor of teal-green moss with tiny softly glowing blue and pink specks, small ferns"),
    ("ground_ruins", "old cracked grey flagstone paving with moss in the joints, some broken slabs and weeds"),
    ("ground_orcfort", "trampled muddy brown dirt with scattered straw, footprints and a few bone fragments"),
    ("ground_swamp", "boggy swamp ground of dark olive mud with shallow murky puddles, duckweed and short reeds"),
    ("ground_desert", "fine golden desert sand with wind ripples and a few tiny pebbles"),
    ("ground_wasteland", "scorched rust-red cracked earth with ash and charred debris"),
    ("ground_graveyard", "dim graveyard ground of sparse dark grass, grey dirt, dead leaves and small stones"),
    ("ground_beach", "pale beach sand with tiny shells, pebbles and damp darker patches"),
    ("ground_anthill", "packed brown earth riddled with small burrow holes, crumbs of soil and roots"),
    ("ground_hell", "dark crimson-black rock ground with glowing ember fissures and scattered cinders"),
    ("town_cobble", "cobblestone road of rounded grey-brown stones with dirt in the joints"),
    ("town_plaza", "large pale square stone slabs of a town plaza, slightly worn, thin joints"),
    ("town_flowerbed", "garden flowerbed of dark soil with small flowers in red, yellow, violet and white and green leaves"),
    ("town_dirt", "packed dirt road with wheel ruts, small stones and dry grass at the edges"),
    ("town_stone", "rough grey stone ground under walls, gravel and chipped stone"),
]


def terrain_prompt(desc):
    return f"{desc}. " + STYLE_TEX


# ─────────────────────────────────────────────────────────────────────────────
# 소품(2x2 한 장에 네 가지) — Props.cpp의 모양과 1:1
# ─────────────────────────────────────────────────────────────────────────────
PROP_SHEETS = [
    ("props_trees_a", ["a round leafy oak tree with a thick trunk and lush green canopy",
                       "a dark green round forest tree with a mossy trunk",
                       "a tall dark evergreen pine tree",
                       "a snow-covered pine tree with heavy snow on its branches"]),
    ("props_trees_b", ["a leafless dead tree with twisted grey branches",
                       "a dead swamp tree with drooping moss and a hollow trunk",
                       "a charred black burnt tree stump with jagged branches",
                       "a tall palm tree with a curved trunk and green fronds"]),
    ("props_plants", ["a round leafy green bush", "a flowering bush with small white and pink blossoms",
                      "a clump of tall swamp reeds and cattails", "a tall desert cactus with two arms"]),
    ("props_rocks_a", ["a medium grey boulder", "a mossy grey boulder with green moss on top",
                       "a snow-capped pale blue-grey boulder", "a red sandstone boulder"]),
    ("props_rocks_b", ["a sandy tan desert rock", "a rust-red scorched rock",
                       "a tall jagged red rock spire", "a tall jagged black obsidian spike"]),
    ("props_magic", ["a cluster of giant glowing mushrooms with pink caps", "a cluster of giant glowing mushrooms with blue caps",
                     "a cluster of glowing cyan crystals growing from the ground", "a cluster of glowing violet crystals"]),
    ("props_ruins", ["a broken ancient stone pillar with carved teal glyphs", "an old weathered gravestone with a rounded top",
                     "a stone grave cross leaning slightly", "a tall pale bone spike jutting from the ground"]),
    ("props_camp", ["a section of sharpened wooden palisade stakes", "a wooden barrel with iron bands",
                    "a pile of old animal bones and a skull", "a large earthen ant mound with holes"]),
    ("props_misc", ["a cracked volcanic vent glowing with lava", "a cut tree stump with rings", "a snowman wearing a scarf",
                    "a small pile of firewood logs"]),
    ("props_town", ["an iron street lamp post with a warm lit lantern", "a market stall with a red-and-white striped awning and crates of fruit",
                    "a market stall with a blue-and-white striped awning and cloth goods", "a round stone town fountain with a small spout"]),
]


def prop_prompt(items):
    return (
        "Sprite sheet of four separate environment props for an isometric 2.5D dark-fantasy MMORPG, arranged in a 2x2 grid, "
        "one prop centered in each quadrant with generous empty margin, each standing upright on an invisible ground, "
        + ISO + ". "
        f"Top-left: {items[0]}. Top-right: {items[1]}. Bottom-left: {items[2]}. Bottom-right: {items[3]}. "
        + STYLE_PROP
    )


# 건물 · 성벽 면 텍스처(정면에서 본 평면 텍스처 — 3D 면에 입힌다)
WALL_TEX = [
    ("wall_plaster", "a whitewashed plaster wall with dark timber framing beams, slightly weathered, viewed straight on"),
    ("wall_stone", "a rough grey fieldstone masonry wall with mortar joints, viewed straight on"),
    ("roof_red", "overlapping red-brown clay roof tiles in rows, viewed straight on"),
    ("roof_slate", "overlapping dark blue-grey slate roof shingles in rows, viewed straight on"),
    ("wood_planks", "dark weathered wooden planks with iron nails, vertical boards, viewed straight on"),
    ("wall_town", "a massive town wall of large pale grey cut stone blocks with moss in the joints, viewed straight on"),
]


def wall_prompt(desc):
    return (f"{desc}. Seamless tileable flat texture, orthographic front view, even lighting, no perspective, fills the whole square, "
            "hand-painted game texture, muted colors, no text, no border.")


# ─────────────────────────────────────────────────────────────────────────────
# 아이콘(3x3) — 아이템 58 + 기술 18 + 상태 9 + 퀘스트 물건 8 + 기타
# ─────────────────────────────────────────────────────────────────────────────
ICON_SHEETS = [
    ("icons_consumables", [
        ("item_1", "a small round glass potion bottle of red herbal healing liquid with a cork and a leaf tag"),
        ("item_8", "a larger glass potion bottle of deep crimson strong herbal potion with a wax seal"),
        ("item_9", "an ornate glass vial of glowing amber sap elixir"),
        ("item_10", "a rolled parchment scroll tied with a red ribbon and a small house seal (return scroll)"),
        ("item_11", "a rolled parchment scroll with glowing teal root-like lines (oldway travel scroll)"),
        ("item_12", "a slim glass bottle of pale green swirling wind potion with a feather tied to it"),
        ("item_13", "a rolled parchment scroll with a glowing teal engraved glyph seal (engraving scroll)"),
        ("item_14", "a golden-edged parchment scroll with a radiant pale-gold glyph seal (consecrated engraving scroll)"),
        ("item_7", "a small stack of thick silver coins stamped with a morning star (grosz)"),
    ]),
    ("icons_swords", [
        ("item_2", "a simple wooden practice sword"),
        ("item_3", "a plain iron short sword"),
        ("item_4", "a polished steel longsword"),
        ("item_5", "a longsword with a blade glowing ember-orange along its edge"),
        ("item_6", "a noble pale-steel greatsword with a gold star-shaped crossguard and a faint dawn glow (Dawnkeeper)"),
        ("item_15", "a well-crafted blacksmith's dagger with a leather-wrapped grip"),
        ("item_16", "a curved cavalry saber with a brass guard"),
        ("item_17", "a knight-commander's black steel longsword with a crimson-and-gold hilt"),
        ("item_18", "a massive jagged executioner's blade of dark metal with glowing red veins"),
    ]),
    ("icons_staves_daggers", [
        ("item_19", "a simple apprentice's wooden staff"),
        ("item_21", "a sturdy oak staff with a knotted head"),
        ("item_22", "a carved staff topped with an engraved stone glowing teal"),
        ("item_23", "a staff of charred wood topped with a smoldering ember crystal"),
        ("item_24", "a living branch staff with leaves and a softly glowing heart-shaped knot (Harven Branch)"),
        ("item_20", "a plain hunting dagger"),
        ("item_25", "a blackened iron dagger"),
        ("item_26", "a slender dagger with a pale moonlight-silver blade"),
        ("item_27", "a green-tinted venom fang dagger dripping poison"),
    ]),
    ("icons_armor_a", [
        ("item_28", "a dark curved dagger shaped like a night beast's fang"),
        ("item_29", "a brown leather hood"),
        ("item_30", "a plain iron helmet"),
        ("item_31", "a knightly order helmet with a blue plume"),
        ("item_32", "a padded quilted cloth armor jacket"),
        ("item_33", "a chainmail shirt"),
        ("item_34", "a steel plate cuirass"),
        ("item_35", "a dark slate-blue engraver's robe with teal glyph embroidery"),
        ("item_36", "a dark hunter's leather armor vest with straps"),
    ]),
    ("icons_armor_b", [
        ("item_37", "a pair of brown leather gloves"),
        ("item_38", "a pair of iron gauntlets"),
        ("item_39", "a pair of brown leather boots"),
        ("item_40", "a pair of iron greaves boots"),
        ("item_41", "a folded brown traveler's cloak"),
        ("item_42", "a folded dark night cloak"),
        ("item_43", "a bone charm amulet on a cord"),
        ("item_44", "a silver pendant necklace"),
        ("item_45", "a copper ring"),
    ]),
    ("icons_sets", [
        ("item_46", "a silver ring"),
        ("item_47", "an ornate pale-gold trimmed knight helmet of the Oath order"),
        ("item_48", "an ornate steel breastplate with a pale-gold star emblem of the Oath order"),
        ("item_49", "ornate steel gauntlets with pale-gold trim of the Oath order"),
        ("item_50", "ornate steel greaves with pale-gold trim of the Oath order"),
        ("item_51", "a teal-embroidered mystic hood (Irerin set)"),
        ("item_52", "a flowing teal and slate mystic robe with glowing glyphs (Irerin set)"),
        ("item_53", "a teal crystal amulet (Irerin set)"),
        ("item_54", "a silver band ring set with a teal stone (Irerin set)"),
    ]),
    ("icons_sets_b", [
        ("item_55", "silver-grey wolf-fur trimmed leather grips (Silverwolf set)"),
        ("item_56", "silver-grey wolf-fur trimmed boots (Silverwolf set)"),
        ("item_57", "a silver-grey wolf pelt cloak (Silverwolf set)"),
        ("item_58", "a silver ring shaped like a wolf head (Silverwolf set)"),
        ("quest_1", "a clump of soft green moss"),
        ("quest_2", "a bundle of dry golden straw tied with twine"),
        ("quest_3", "a chunk of raw meat on a bone"),
        ("quest_4", "a small glowing wisp of pale spirit light in a glass jar"),
        ("quest_5", "a glob of glowing amber tree sap"),
    ]),
    ("icons_skills_a", [
        ("quest_6", "a battered knight's crest badge with a star emblem"),
        ("quest_7", "a small glowing ember in a blackened iron cage"),
        ("quest_8", "a smooth pebble carved with a glowing teal glyph"),
        ("skill_1", "skill icon: a heavy sword slash arc in pale gold light"),
        ("skill_2", "skill icon: a round shield bashing forward with a burst of stars"),
        ("skill_3", "skill icon: a sword spinning in a full circular whirlwind"),
        ("skill_4", "skill icon: a knight charging forward with a streak of speed lines of light"),
        ("skill_5", "skill icon: a shield glowing with a protective golden aura"),
        ("skill_6", "skill icon: a great sword striking down with a column of dawn light (judgment)"),
    ]),
    ("icons_skills_b", [
        ("skill_7", "skill icon: a glowing teal engraved glyph bolt flying forward"),
        ("skill_8", "skill icon: a frost glyph circle with ice crystals"),
        ("skill_9", "skill icon: a ring of fire bursting outward"),
        ("skill_10", "skill icon: a figure stepping through a teal root-portal (oldway leap)"),
        ("skill_11", "skill icon: a chain of forked lightning jumping between points"),
        ("skill_12", "skill icon: a falling star meteor streaking down"),
        ("skill_13", "skill icon: two quick dagger stabs crossing"),
        ("skill_14", "skill icon: a shadowy silhouette stepping out of darkness behind a target"),
        ("skill_15", "skill icon: a dagger blade dripping green poison"),
    ]),
    ("icons_skills_c", [
        ("skill_16", "skill icon: a blurred afterimage of a dodging figure"),
        ("skill_17", "skill icon: a dagger stabbing into a glowing weak point from behind"),
        ("skill_18", "skill icon: a dark crescent moon with five slashing blade trails"),
        ("status_1", "status icon: spinning stars above a dazed head (stunned)"),
        ("status_2", "status icon: a block of ice (frozen)"),
        ("status_3", "status icon: a small flame (burning)"),
        ("status_4", "status icon: a green poison droplet with a skull-like bubble (poisoned)"),
        ("status_5", "status icon: a snail shell with a blue chill aura (slowed)"),
        ("status_6", "status icon: a shield with a golden glow (guarding)"),
    ]),
    ("icons_status_b", [
        ("status_7", "status icon: a dagger with a gleaming critical spark (ambush)"),
        ("status_8", "status icon: a faint ghostly afterimage silhouette (evading)"),
        ("status_9", "status icon: a glowing teal engraved mark on skin (marked)"),
        ("ui_coin", "a single thick silver coin stamped with a morning star"),
        ("ui_exp", "a small glowing pale-gold diamond-shaped star gem"),
        ("ui_quest", "a rolled quest letter with a wax seal"),
        ("ui_map", "a folded parchment map"),
        ("ui_bag", "a leather satchel bag"),
        ("ui_char", "a knight's helmet in profile"),
    ]),
]


def icon_prompt(items):
    names = [f"Row {1 + i // 3}, column {1 + i % 3}: {d}." for i, (_, d) in enumerate(items)]
    return (
        "Sheet of nine separate game icons arranged in a 3x3 grid of equal square cells, one object per cell, same scale and lighting for all. "
        + " ".join(names) + " " + STYLE_ICON
    )


# ─────────────────────────────────────────────────────────────────────────────
# 효과(검은 바탕 — 더하기 혼합) · 연기(투명 바탕)
# ─────────────────────────────────────────────────────────────────────────────
FX_SHEETS = [
    ("fx_slash", ["a wide crescent sword slash arc of bright white-gold light, thin trailing edge",
                  "a heavy thick crescent slash arc of white light with sparks",
                  "a straight thrust streak of white light",
                  "three parallel claw slash marks of bright orange-red light"], "black"),
    ("fx_impact", ["a sharp star-shaped hit impact burst of white-yellow light with radiating spikes",
                   "a large critical hit burst of white and gold light with flying shards",
                   "a round shockwave ring impact of pale light",
                   "a teal magical impact burst with glyph fragments"], "black"),
    ("fx_fire", ["a bright flickering flame tongue", "a round fireball with a burning core",
                 "a cluster of floating glowing embers", "a ring of fire seen from above at an angle"], "black"),
    ("fx_ice", ["an outward burst of sharp ice shards", "a single large glowing frost crystal",
                "a delicate glowing snowflake", "a jagged cluster of ice spikes erupting upward"], "black"),
    ("fx_lightning", ["a vertical jagged lightning bolt", "a forked branching lightning bolt",
                      "a short curved electric arc", "a crackling ball of electric sparks"], "black"),
    ("fx_magic", ["a circular engraved glyph circle of glowing teal lines (magic circle viewed flat)",
                  "a single glowing teal glyph symbol", "a glowing teal energy orb with a bright core",
                  "a tall vertical pillar beam of soft teal light"], "black"),
    ("fx_holy", ["sparkling golden healing motes rising", "a tall vertical pillar of warm golden dawn light",
                 "a radiant golden ring halo", "a translucent golden dome shield bubble"], "black"),
    ("fx_shadow", ["a swirling cloud of green poison gas", "a splash of glowing green venom droplets",
                   "a burst of dark violet shadow smoke", "a quick bright dagger flash streak"], "black"),
    ("fx_smoke", ["a soft grey dust puff cloud", "a billowing dark smoke cloud",
                  "a small kicked-up dirt burst with pebbles", "a wispy white mist puff"], "transparent"),
    ("fx_loot", ["a bright vertical loot light beam", "a twinkling four-pointed star glint",
                 "a spray of golden coin sparkles", "a soft round glow orb"], "black"),
]


def fx_prompt(items, bg):
    grid = (f"Top-left: {items[0]}. Top-right: {items[1]}. Bottom-left: {items[2]}. Bottom-right: {items[3]}. ")
    if bg == "black":
        return "Four separate effect elements in a 2x2 grid. " + grid + STYLE_FX
    return ("Game visual-effect sprite sheet with four separate soft elements in a 2x2 grid, each centered in its quadrant with empty margin. "
            + grid + "Painterly soft edges, semi-transparent wisps. Transparent background, no text, no border, no grid lines.")


# ─────────────────────────────────────────────────────────────────────────────
# UI
# ─────────────────────────────────────────────────────────────────────────────
UI_ITEMS = [
    ("ui_panel_tex", "Seamless tileable texture of dark slate-blue stone with very faint engraved flowing root-like glyph lines and subtle grain, "
                     "very low contrast so text on top stays readable, orthographic flat, even lighting, no border, no text.", "opaque", "1:1"),
    ("ui_ornaments", "Four separate UI ornament pieces for a dark-fantasy MMORPG interface in a 2x2 grid on a transparent background: "
                     "top-left: an L-shaped engraved dark bronze corner ornament made for the TOP-LEFT corner of a window frame "
                     "(its corner point at the top-left, arms running right and down), with a small amber diamond gem at the corner; "
                     "top-right: a long thin horizontal engraved bronze divider line with a small central diamond star; "
                     "bottom-left: an engraved dark bronze title plaque with a chamfered shape and empty center; "
                     "bottom-right: a circular engraved bronze frame ring with four small diamond studs. "
                     "Hand-painted, restrained, crisp, no text, no grid lines.", "transparent", "1:1"),
    ("ui_cursors", "Four separate mouse cursor icons for a dark-fantasy MMORPG in a 2x2 grid on a transparent background, each pointing to the top-left corner: "
                   "top-left: an engraved dark steel arrow pointer with a bronze edge, "
                   "top-right: a small sword pointing to the top-left (attack cursor), "
                   "bottom-left: an open hand reaching (pick up cursor), "
                   "bottom-right: a small speech scroll (talk cursor). Hand-painted, crisp silhouettes, no text, no grid lines.", "transparent", "1:1"),
]


def all_requests():
    """(key, params) 목록 — generate_image_batch에 그대로 넣는다."""
    base = {"model": "gpt_image_2_5", "quality": "medium", "resolution": "1k", "aspect_ratio": "1:1"}
    out = []
    for cls, job in PLAYER_BASE_JOBS.items():
        for sheet in ("act", "cmb", "back"):
            if cls == "knight" and sheet == "act":
                continue   # 이미 생성함(8449d4c6-...)
            out.append((f"player_{cls}_{sheet}", dict(base, background="transparent", prompt=player_prompt(cls, sheet),
                                                     medias=[{"role": "image_references", "value": job}])))
    for mid, key, arch, desc in MONSTERS:
        out.append((f"mon_{mid:02d}_{key}", dict(base, background="transparent", prompt=monster_prompt(arch, desc))))
    for key, pid, desc in NPCS:
        out.append((key, dict(base, background="transparent", prompt=npc_prompt(desc),
                              medias=[{"role": "image_references", "value": PORTRAIT_JOBS[pid]}])))
    for key, desc, poses in ANIMALS:
        out.append((key, dict(base, background="transparent", prompt=animal_prompt(desc, poses))))
    for key, desc in TERRAIN:
        out.append((key, dict(base, background="opaque", prompt=terrain_prompt(desc))))
    for key, items in PROP_SHEETS:
        out.append((key, dict(base, background="transparent", prompt=prop_prompt(items))))
    for key, desc in WALL_TEX:
        out.append((key, dict(base, background="opaque", prompt=wall_prompt(desc))))
    for key, items in ICON_SHEETS:
        out.append((key, dict(base, background="transparent", prompt=icon_prompt(items))))
    for key, items, bg in FX_SHEETS:
        out.append((key, dict(base, background=("opaque" if bg == "black" else "transparent"), prompt=fx_prompt(items, bg))))
    for key, prompt, bg, ar in UI_ITEMS:
        out.append((key, dict(base, background=bg, prompt=prompt, aspect_ratio=ar)))
    return out


if __name__ == "__main__":
    reqs = all_requests()
    print(len(reqs), "requests,", len(reqs) * 0.5, "credits (medium 1k)")
    for k, p in reqs:
        print(k, len(p["prompt"]))
