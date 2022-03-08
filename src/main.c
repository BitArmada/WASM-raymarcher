#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include <emscripten.h>

#define THREAD_SIZE 10

#define EPSILON 0.0001
// #define MAX_MARCHING_STEPS 200
// #define SAMPLES 40
// #define MAX_BOUNCES 8
// #define SPREAD 5
int MAX_MARCHING_STEPS = 200;
int SAMPLES = 40;
int MAX_BOUNCES = 8;
int SPREAD = 5;
#define OBJECTS 5

#define max(a,b) (((a) > (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))
#define grid(pos) ((int)abs(pos.x-3)%2 + (int)abs(pos.z-3)%2 == 1)

extern void displayProgress(float progress);
// extern void print(int val);

int currentBounces = 0;

EMSCRIPTEN_KEEPALIVE
void setConstants(int steps, int samples, int bounces, int spread){
    MAX_MARCHING_STEPS = steps;
    SAMPLES = samples;
    MAX_BOUNCES = bounces;
    SPREAD = spread;
}

struct Vector {
    float x;
    float y;
    float z;
};

struct Object {
    struct Vector position;
    struct Vector scale;
    struct Vector color;
    float (*DE)(struct Vector, struct Vector, struct Vector);
    struct Vector (*shader)(struct Object, struct Vector, struct Vector);
};

int currentObject = -1;

struct Object Objects[OBJECTS];

struct Vector Multiply(struct Vector a, struct Vector b){
    struct Vector result;
    result.x = a.x*b.x;
    result.y = a.y*b.y;
    result.z = a.z*b.z;
    return result;
}

struct Vector Divide(struct Vector a, struct Vector b){
    struct Vector result;
    result.x = a.x/b.x;
    result.y = a.y/b.y;
    result.z = a.z/b.z;
    return result;
}

struct Vector scalarDivide(struct Vector a, float b){
    struct Vector result;
    result.x = a.x/b;
    result.y = a.y/b;
    result.z = a.z/b;
    return result;
}

struct Vector Scale(struct Vector a, float b){
    struct Vector result;
    result.x = a.x*b;
    result.y = a.y*b;
    result.z = a.z*b;
    return result;
}

struct Vector Add(struct Vector a, struct Vector b){
    struct Vector result;
    result.x = a.x+b.x;
    result.y = a.y+b.y;
    result.z = a.z+b.z;
    return result;
}

struct Vector Subtract(struct Vector a, struct Vector b){
    struct Vector result;
    result.x = a.x-b.x;
    result.y = a.y-b.y;
    result.z = a.z-b.z;
    return result;
}

struct Vector Abs(struct Vector a){
    struct Vector result;
    result.x = abs(a.x);
    result.y = abs(a.y);
    result.z = abs(a.z);
    return result;
}

float Dot(struct Vector a, struct Vector b){
    return (a.x*b.x+a.y*b.y+a.z*b.z);
}

int nearZero(struct Vector v){
    const float s = 1e-8;
    return (fabs(v.x) < s) && (fabs(v.y) < s) && (fabs(v.z) < s);
}

float Magnitude(struct Vector a){
    return sqrt((a.x*a.x) + (a.y*a.y) + (a.z*a.z));
}

struct Vector Normalize(struct Vector a){
    struct Vector result;
    result = scalarDivide(a, Magnitude(a));
    return result;
}
struct Vector Max(struct Vector a, float b){
    struct Vector result;
    result.x = max(a.x, b);
    result.y = max(a.y, b);
    result.z = max(a.z, b);
    return result;
}

struct Vector Negative(struct Vector a){
    struct Vector result;
    result.x = -a.x;
    result.y = -a.y;
    result.z = -a.z;
    return result;
}

struct Vector Mod(struct Vector a, int b){
    struct Vector result;
    result.x = (int)a.x%b;
    result.y = (int)a.y%b;
    result.z = (int)a.z%b;
    return result;
}

float randomFloat(){
    return (float)rand()/(float)(RAND_MAX);
}

float randomRange(float range){
    return (randomFloat()*range*2)-range;
}

struct Vector randomVector(){
    return (struct Vector){randomRange(1), randomRange(1), randomRange(1)};
}

struct Vector randomInUnitSphere(){
    while(1){
        struct Vector r = randomVector();
        if(Magnitude(r)<1){
            return r;
        }
    }
}

static float reflectance(float cosine, float ref_idx) {
    // Use Schlick's approximation for reflectance.
    float r0 = (1-ref_idx) / (1+ref_idx);
    r0 = r0*r0;
    return r0 + (1-r0)*pow((1 - cosine),5);
}

struct Vector reflect(struct Vector v, struct Vector n) {
    return Subtract(v, Scale(n, Dot(v,n)*2));
}

struct Vector refract(struct Vector uv, struct Vector n, double etai_over_etat) {
    float cos_theta = min(Dot(Negative(uv), n), 1.0);
    struct Vector r_out_perp =  Scale(Add(uv, Scale(n, cos_theta)), etai_over_etat);
    struct Vector r_out_parallel = Scale(n, -sqrt(fabs(1.0 - pow(Magnitude(r_out_perp), 2) ) ) );
    return Add(r_out_perp, r_out_parallel);
}

float vectorAverage(struct Vector v){
    return (v.x+v.y+v.z)/3;
}

struct Vector estimateNormal(struct Object obj, struct Vector p) {
    struct Vector output = {
        obj.DE((struct Vector){p.x + EPSILON, p.y, p.z}, obj.position, obj.scale) - obj.DE((struct Vector){p.x - EPSILON, p.y, p.z}, obj.position, obj.scale),
        obj.DE((struct Vector){p.x, p.y + EPSILON, p.z}, obj.position, obj.scale) - obj.DE((struct Vector){p.x, p.y - EPSILON, p.z}, obj.position, obj.scale),
        obj.DE((struct Vector){p.x, p.y, p.z  + EPSILON}, obj.position, obj.scale) - obj.DE((struct Vector){p.x, p.y, p.z - EPSILON}, obj.position, obj.scale)
    };
    return Normalize(output);
}

float sphereDE(struct Vector v, struct Vector p, struct Vector s){
    return sqrt(pow(v.x-p.x, 2)+pow(v.y-p.y, 2)+pow(v.z-p.z, 2))-s.x;
}

float planeDE(struct Vector v, struct Vector p, struct Vector s){
    return (-v.y+(p.y));
}

float boxDE(struct Vector v, struct Vector p, struct Vector s){
    struct Vector d = Subtract(Abs(v), s);
    return min(max(d.x,max(d.y,d.z)),0.0) + Magnitude(Max(d,0.0));
}

struct Vector ray(struct Vector start, struct Vector dir){
    float depth = 0;
    struct Vector color = {1,1,1};
    if(currentBounces>MAX_BOUNCES){
        return color;
    }
    for(int i = 0; i < MAX_MARCHING_STEPS; i++){
        struct Vector pos = Add(start, Scale(dir, depth));
        float distance = 99999;
        struct Object obj;
        int index = -1;
        // find min object
        for(int i = 0; i < OBJECTS; i++){
            if(i == currentObject){
                continue;
            }
            float d = Objects[i].DE(
                pos, 
                Objects[i].position,
                Objects[i].scale
            );
            if(d < distance){
                distance = d;
                obj = Objects[i];
                index = i;
            }
        }
        if(distance < EPSILON){
            currentBounces++;
            // color = estimateNormal(obj, pos);
            // color = Add(color, (struct Vector){1,1,1});
            // color = scalarDivide(color, 2);
            currentObject = index;
            color = obj.shader(obj, pos, dir);
            currentObject = -1;
            return color;
        }else if(distance > 200){
            return color;
        }
        depth += distance;
    }
    
    return color;
}

struct Vector Lambertian(struct Object obj, struct Vector pos, struct Vector dir){
    currentObject = -1;
    struct Vector color = obj.color;
    struct Vector normal = estimateNormal(obj, pos);
    struct Vector scatterDir = Normalize(Add(normal, Normalize(randomInUnitSphere())));
    if(nearZero(scatterDir)){
        scatterDir = normal;
    }
    struct Vector bounceColor = ray(Add(pos, Scale(normal, 0.001)), scatterDir);
    color = scalarDivide(Add(bounceColor, color), 2);
    //color = scalarDivide(Add(normal, (struct Vector){1,1,1}), 2);
    return color;
}

struct Vector LambertianGrid(struct Object obj, struct Vector pos, struct Vector dir){
    currentObject = -1;
    struct Vector color = obj.color;
    if(!grid(pos)){
        color = (struct Vector){obj.color.z, obj.color.x, obj.color.y};
    }

    struct Vector normal = estimateNormal(obj, pos);
    struct Vector scatterDir = Normalize(Add(normal, Normalize(randomInUnitSphere())));
    if(nearZero(scatterDir)){
        scatterDir = normal;
    }
    struct Vector bounceColor = ray(Add(pos, Scale(normal, 0.001)), scatterDir);
    color = scalarDivide(Add(bounceColor, color), 2);
    //color = scalarDivide(Add(normal, (struct Vector){1,1,1}), 2);
    return color;
}

struct Vector Metal(struct Object obj, struct Vector pos, struct Vector dir){
    currentObject = -1;
    float fuzz = 0;
    struct Vector color = obj.color;
    struct Vector normal = estimateNormal(obj, pos);
    struct Vector scatterDir = reflect(Normalize(dir), normal);
    struct Vector bounceColor = ray(Add(pos, Scale(normal, 0.001)), Add(scatterDir, Scale(Normalize(randomInUnitSphere()), fuzz)));
    color = scalarDivide(Add(bounceColor, color), 2);
    return color;
}

struct Vector Glass(struct Object obj, struct Vector pos, struct Vector dir){
    float refractionRatio = 1.7;
    struct Vector color = obj.color;
    struct Vector normal = estimateNormal(obj, pos);

    struct Vector d = Normalize(dir);

    float cos_theta = min(Dot(Negative(d), normal), 1.0);
    float sin_theta = sqrt(1.0 - cos_theta*cos_theta);

    int cannot_refract = refractionRatio * sin_theta > 1.0;
    struct Vector direction;

    if (cannot_refract || reflectance(cos_theta, refractionRatio) > randomFloat()){
        direction = reflect(d, normal);
    }else{
        direction = refract(d, normal, refractionRatio);
    }

    //struct Vector refracted = refract(Normalize(d), normal, refractionRatio);
    struct Vector bounceColor = ray(Add(pos, Scale(normal, 0.001)), direction);
    color = Scale(bounceColor,1);//Scale(color, reflectance(cos_theta, refractionRatio));
    //color = scalarDivide(Add(refracted, (struct Vector){1,1,1}), 2);
    return color;
}

EMSCRIPTEN_KEEPALIVE
void initObjects(){
    Objects[0] = (struct Object){
        (struct Vector){0,0,0}, // position
        (struct Vector){1,1,1}, // scale
        (struct Vector){0.0,0.0,0.0}, // color
        sphereDE,
        Lambertian
    };

    Objects[1] = (struct Object){
        (struct Vector){1.4,0,0}, // position
        (struct Vector){0.8,0.8,0.8}, // scale
        (struct Vector){0.2,0.2,0.2}, // color
        sphereDE,
        Metal
    };

    Objects[2] = (struct Object){
        (struct Vector){-1.8,0.2,0}, // position
        (struct Vector){0.5,0.5,0.5}, // scale
        (struct Vector){0.2,0.2,0.2}, // color
        sphereDE,
        Glass
    };
    Objects[3] = (struct Object){
        (struct Vector){-1.8,0.2,-1}, // position
        (struct Vector){1,1,1}, // scale
        (struct Vector){0.2,0.2,0.2}, // color
        sphereDE,
        Glass
    };

    Objects[4] = (struct Object){
        (struct Vector){0,1,0}, // position
        (struct Vector){1,1,1}, // scale
        (struct Vector){1,0,0}, // color
        planeDE,
        LambertianGrid
    };
}

EMSCRIPTEN_KEEPALIVE
int draw(uint8_t *pixels, int WIDTH, int HEIGHT){
    struct Vector position = {0,-0.5,-6};

    // Objects[0] = (struct Object){
    //     (struct Vector){0,0,0}, // position
    //     (struct Vector){1,1,1}, // scale
    //     (struct Vector){0.0,0.0,0.0}, // color
    //     sphereDE,
    //     Lambertian
    // };

    // Objects[1] = (struct Object){
    //     (struct Vector){1.4,0,0}, // position
    //     (struct Vector){0.8,0.8,0.8}, // scale
    //     (struct Vector){0.2,0.2,0.2}, // color
    //     sphereDE,
    //     Metal
    // };

    // Objects[2] = (struct Object){
    //     (struct Vector){-1.8,0.2,0}, // position
    //     (struct Vector){0.5,0.5,0.5}, // scale
    //     (struct Vector){0.2,0.2,0.2}, // color
    //     sphereDE,
    //     Glass
    // };
    // Objects[3] = (struct Object){
    //     (struct Vector){-1.8,0.2,-1}, // position
    //     (struct Vector){1,1,1}, // scale
    //     (struct Vector){0.2,0.2,0.2}, // color
    //     sphereDE,
    //     Glass
    // };

    // Objects[4] = (struct Object){
    //     (struct Vector){0,1,0}, // position
    //     (struct Vector){1,1,1}, // scale
    //     (struct Vector){1,0,0}, // color
    //     planeDE,
    //     LambertianGrid
    // };
    int ind = 0;
    int pixelsLength = WIDTH*HEIGHT*4;
    // uint8_t *pixels = malloc(WIDTH*HEIGHT*3);

    for(int hy = -(HEIGHT/2); hy < HEIGHT/2; hy++){
        for(int wx = -(WIDTH/2); wx < WIDTH/2; wx++){

            struct Vector dir = {(float)wx/WIDTH, (float)hy/HEIGHT, 1};
            struct Vector result = (struct Vector){0,0,0};//(struct Vector){1, (float)wx/WIDTH, (float)hy/HEIGHT};//dir;//{0,0,0};

            for(int i = 0; i < SAMPLES; i++){
                currentBounces = 0;
                struct Vector randomDir = {dir.x+(randomRange(1)/(WIDTH*SPREAD)), dir.y+(randomRange(1)/(HEIGHT*SPREAD)), 1};
                result = Add(result, ray(position, Normalize(randomDir)));
            }

            //result = scalarDivide(result, SAMPLES);

            // gamma correction
            float scale = 1.0 / SAMPLES;
            result.x = sqrt(scale * result.x);
            result.y = sqrt(scale * result.y);
            result.z = sqrt(scale * result.z);

            //printWithColorBlock(result);
            pixels[ind++] = (uint8_t)(result.x*255);
            pixels[ind++] = (uint8_t)(result.y*255);
            pixels[ind++] = (uint8_t)(result.z*255);
            pixels[ind++] = (uint8_t)(255);
        }
        displayProgress(((float)ind/(float)pixelsLength));
    }

    return 0;
}