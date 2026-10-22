#include <audition/memory/in_memory_registries.hpp>
#include <audition/memory/sound_prototype_matcher.hpp>
#include <audition/model/in_memory_model_registry.hpp>

#include <exception>
#include <iostream>
#include <stdexcept>

int main() {
    using namespace audition;
    try {
        InMemorySpeakerRegistry speakers;
        InMemoryAcousticSourceRegistry sources;
        const auto speaker_id=speakers.create("demonstration speaker");
        const auto source_id=sources.create();
        sources.associateSpeaker(source_id,speaker_id);
        const auto source=sources.find(source_id);
        if(!source || !source->associated_speaker ||
           *source->associated_speaker!=speaker_id)
            throw std::runtime_error("source/speaker association failed");
        std::cout << "source=" << source_id.value()
                  << " speaker=" << speaker_id.value() << '\n';

        InMemorySoundPrototypeRegistry prototypes;
        const auto bell=prototypes.create("bell");
        const auto motor=prototypes.create("motor");
        prototypes.addExample(bell,AudioEmbedding{"demo-audio-embed-v1",{1.F,0.F},{}});
        prototypes.addExample(motor,AudioEmbedding{"demo-audio-embed-v1",{0.F,1.F},{}});
        CosineSoundPrototypeMatcher matcher{prototypes};
        const auto found=matcher.match(
            AudioEmbedding{"demo-audio-embed-v1",{0.98F,0.02F},{}});
        if(found.empty() || found.front().prototype_id!=bell ||
           found.front().label!="bell")
            throw std::runtime_error("prototype cosine ranking failed");
        std::cout << "nearest_sound=" << found.front().label
                  << " cosine=" << found.front().similarity.value << '\n';

        InMemoryModelRegistry models;
        ModelDescriptor descriptor{};
        descriptor.id="demo-classifier";
        descriptor.version="1";
        descriptor.task="sound_classification";
        descriptor.backend="example-only";
        descriptor.license.spdx_id="UNVERIFIED";
        descriptor.license.redistribution=false;
        models.add(descriptor);
        const auto model=models.find(descriptor.id);
        if(!model || model->backend!="example-only" || models.list().size()!=1U)
            throw std::runtime_error("model metadata catalog failed");
        std::cout << "model_id=" << model->id
                  << " model_backend=" << model->backend << '\n';
        std::cout << "registries=ok\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "registries=error reason=" << error.what() << '\n';
        return 1;
    }
}
