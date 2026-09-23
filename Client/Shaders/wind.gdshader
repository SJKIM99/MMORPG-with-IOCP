shader_type spatial;
// Quaternius 나뭇잎은 alphaMode = MASK (컷오프 0.2) 에 doubleSided = true 인
// 알파 컷아웃 쿼드다. 알파를 버리면 잎 모양이 잘리지 않아 사각 덩어리로 보이고,
// 뒷면 법선을 뒤집지 않으면 안쪽 면이 검게 나온다. 둘 다 해 줘야 한다.
render_mode cull_disabled, diffuse_burley, specular_schlick_ggx;

// 식생 흔들림. 정점을 흔들 뿐 원본 머티리얼의 색은 그대로 쓴다.
//
// KayKit / Quaternius 는 메시 한 장에 텍스처 한 장을 쓰므로
// albedo 텍스처만 받아 넘기면 원래 모습이 유지된다.
// 텍스처가 없는 모델(정점 색)은 albedo_color 만으로 그린다.

uniform sampler2D albedo_texture : source_color, filter_linear_mipmap;
uniform bool has_texture = true;
uniform vec4 albedo_color : source_color = vec4(1.0);
uniform float roughness_value : hint_range(0.0, 1.0) = 1.0;
// 원본 머티리얼의 alphaCutoff. 0 이면 컷아웃을 쓰지 않는다.
uniform float alpha_cutoff : hint_range(0.0, 1.0) = 0.2;

group_uniforms Wind;
// 흔들림은 밑동에서 0, 꼭대기에서 최대가 되어야 뿌리가 떠 보이지 않는다.
uniform float sway_strength = 0.12;   // 높이 1m 당 최대 변위(m)
uniform float sway_speed = 1.1;
uniform float gust_scale = 0.035;     // 바람이 맵을 훑는 파장(1/m)
uniform vec2 wind_dir = vec2(0.82, 0.57);
// 이 높이 아래는 거의 흔들리지 않는다. 줄기 밑동을 붙들어 둔다.
uniform float anchor_height = 0.25;

void vertex() {
	// 월드 좌표를 써야 나무마다 위상이 달라진다.
	// 모델 좌표로 하면 숲 전체가 한 몸처럼 움직인다.
	vec3 world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;

	float phase = dot(world.xz, normalize(wind_dir)) * gust_scale;
	float t = TIME * sway_speed;

	// 큰 흔들림 + 잔떨림. 주파수를 무리수에 가깝게 두어 주기가 겹치지 않게 한다.
	float swing = sin(t + phase) * 0.7 + sin(t * 2.37 + phase * 1.9) * 0.3;

	// 밑동 고정: 로컬 높이에 비례해 흔든다.
	float h = max(VERTEX.y - anchor_height, 0.0);
	vec2 offset = normalize(wind_dir) * swing * sway_strength * h;

	VERTEX.x += offset.x;
	VERTEX.z += offset.y;
}

void fragment() {
	vec4 base = albedo_color;
	if (has_texture) {
		base *= texture(albedo_texture, UV);
	}

	ALBEDO = base.rgb;
	ROUGHNESS = roughness_value;
	SPECULAR = 0.2;

	if (alpha_cutoff > 0.0) {
		ALPHA = base.a;
		ALPHA_SCISSOR_THRESHOLD = alpha_cutoff;
	}

	// 양면 렌더라 뒷면은 법선이 반대다. 뒤집지 않으면 안쪽 잎이 검게 죽는다.
	if (!FRONT_FACING) {
		NORMAL = -NORMAL;
	}
}
