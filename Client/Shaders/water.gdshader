shader_type spatial;
// 물 아래로 들어가면 수면을 밑에서 보게 된다.
render_mode cull_disabled;

// 수면. 텍스처 없이 절차적으로 만든다.
//
// CLAUDE.md 1장 — 이미지를 만들 수 없으므로 노멀맵을 쓸 수 없다. 대신 두 방향
// 으로 흐르는 값 노이즈의 기울기에서 법선을 직접 구한다. 하늘 셰이더와 같은 수법이다.
//
// 수심과 흐름은 **정점 색에 구워 온다**(WaterMesh). 화면공간 깊이 버퍼를 읽으면
// 물 위에 뜬 물체(다리, 물레방아 바퀴)까지 깊이로 잡혀 테두리가 번진다.
//
// 정점 색은 RGBA8 로 저장되어 **0~1 로 잘린다.** 그래서 수심을 미터로 그냥
// 넣을 수 없다. max_depth 로 나눠 넣고 여기서 되돌린다.

uniform vec3 shallow_color : source_color = vec3(0.239, 0.478, 0.573);
uniform vec3 deep_color : source_color = vec3(0.082, 0.255, 0.310);
uniform vec3 foam_color : source_color = vec3(0.875, 0.945, 0.961);

uniform float max_depth = 4.0;       // 정점 색에 수심을 정규화한 기준(m)
uniform float wave_scale = 0.22;     // 잔물결 크기(1/m)
uniform float wave_speed = 0.30;
uniform float wave_strength = 0.55;  // 법선 흔들림. 1.0 이면 45도까지 기운다
uniform float foam_width = 0.45;     // 물가 거품이 끼는 수심(m)
uniform float deep_at = 2.2;         // 이 수심이면 완전히 깊은 색
uniform float base_alpha = 0.62;
uniform float roughness_water : hint_range(0.0, 1.0) = 0.45;

float hash(vec2 p) {
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float value_noise(vec2 p) {
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	float a = hash(i);
	float b = hash(i + vec2(1.0, 0.0));
	float c = hash(i + vec2(0.0, 1.0));
	float d = hash(i + vec2(1.0, 1.0));
	return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// 매 옥타브 비스듬히 돌린다. 값 노이즈는 정사각 격자 위에서 만들어져
// 무늬가 축에 정렬되는데, 그대로 두면 지형 격자(필드는 8m)와 겹쳐 호수가
// 바둑판처럼 보인다.
const mat2 TILT = mat2(vec2(0.819, 0.574), vec2(-0.574, 0.819));   // 35도

// 옥타브를 겹친다(FBM). 한두 층만 쓰면 격자 경계에서 **기울기가 불연속**이라
// 법선에 각이 지고, 반사가 그 각을 그대로 드러내 수면이 종잇조각처럼 보인다.
// 노이즈 값 자체는 부드러운데 미분이 그렇지 않은 것이 원인이라, 값만 보고는
// 무엇이 잘못됐는지 알기 어렵다. 하늘 셰이더에서 구름에 쓴 것과 같은 수법이다.
float fbm(vec2 p) {
	float sum = 0.0;
	float amp = 0.5;
	for (int i = 0; i < 4; i++) {
		sum += value_noise(p) * amp;
		p = TILT * p * 2.03;
		amp *= 0.5;
	}
	return sum;
}

// 서로 다른 방향·속도로 흐르는 두 층. 한 층만 쓰면 무늬가 통째로 미끄러진다.
float waves(vec2 p, float t) {
	float a = fbm(p + vec2(t, t * 0.6));
	float b = fbm(TILT * p * 1.7 - vec2(t * 0.7, t * 1.3));
	return a * 0.6 + b * 0.4;
}

varying float v_depth;
varying float v_flow;
varying vec3 v_world;

void vertex() {
	// r = 수심 / max_depth, g = 흐름 세기(0~1).
	v_depth = COLOR.r * max_depth;
	v_flow = COLOR.g;

	// fragment 의 VERTEX 는 **뷰 공간**이다. 월드 좌표로 무늬를 깔려면
	// 여기서 넘겨야 한다. 뷰 공간 좌표를 쓰면 무늬가 카메라를 따라 흐른다.
	v_world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

void fragment() {
	vec2 p = v_world.xz * wave_scale;
	float t = TIME * wave_speed;

	// 기울기에서 법선을 만든다. 노멀맵 텍스처 없이 잔물결을 내는 방법이다.
	//
	// y 를 1.0 으로 두는 것이 중요하다. 작게 두면 법선이 45도씩 기울어
	// 수면 전체가 구겨진 은박지처럼 번쩍인다 — 실제로 그렇게 나왔다.
	float e = 0.6;
	float h = waves(p, t);
	float hx = waves(p + vec2(e, 0.0), t);
	float hz = waves(p + vec2(0.0, e), t);

	float strength = wave_strength * (1.0 + v_flow * 2.0);
	vec3 n = normalize(vec3((h - hx) * strength, 1.0, (h - hz) * strength));
	NORMAL = normalize((VIEW_MATRIX * vec4(n, 0.0)).xyz);

	// 얕은 곳은 밝고 깊은 곳은 어둡다.
	float deep = clamp(v_depth / deep_at, 0.0, 1.0);
	vec3 base = mix(shallow_color, deep_color, deep);

	// 거품 — 물가(얕은 곳)와 급류에만 낀다.
	// 물결 무늬를 문턱으로 써서 매 프레임 모양이 바뀌게 한다.
	float edge = 1.0 - smoothstep(0.0, foam_width, v_depth);
	float churn = smoothstep(0.52, 0.78, h) * smoothstep(0.15, 0.6, v_flow);
	float foam = clamp(edge * 0.85 + churn, 0.0, 1.0);

	ALBEDO = mix(base, foam_color, foam);

	// 얕을수록 투명해 바닥이 비친다. 거품은 불투명하다.
	ALPHA = clamp(mix(base_alpha + deep * 0.35, 1.0, foam), 0.0, 1.0);

	// 거칠기를 낮게 두면 안 된다.
	//
	// 0.18 로 뒀더니 호수 전체가 하얗게 떴다. 거의 수평인 수면을 낮은 각도로
	// 보면 매끈할수록 **하늘을 그대로 비춘다**(프레넬). 물리적으로는 맞지만
	// 스타일라이즈 화면에서는 물색이 통째로 사라진다.
	// 수심·거품 데이터가 멀쩡한데도 하얘서 한참 헤맸다 — 조명 문제였다.
	ROUGHNESS = mix(roughness_water, 0.8, foam);
	METALLIC = 0.0;
	SPECULAR = 0.25;
}
