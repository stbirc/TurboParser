// Copyright (c) 2012-2013 Andre Martins
// All Rights Reserved.
//
// This file is part of TurboParser 2.1.
//
// TurboParser 2.1 is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TurboParser 2.1 is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with TurboParser 2.1.  If not, see <http://www.gnu.org/licenses/>.

#include "EntityDictionary.h"
#include "EntityOptions.h"
#include "EntityPipe.h"
#include <algorithm>

void EntityDictionary::CreateTagDictionary(SequenceReader *reader) {
  SequenceDictionary::CreateTagDictionary(reader);

  // TODO: the SplitEntityTag function should probably be elsewhere and not on
  // EntityInstance.
  EntityInstance instance;
  Alphabet entities;

  // Display information about the entity tags.
  LOG(INFO) << "Found " << tag_alphabet_.size() << " entity tags:";
  for (Alphabet::iterator it = tag_alphabet_.begin();
       it != tag_alphabet_.end(); ++it) {
    std::string entity_tag = it->first;
    LOG(INFO) << entity_tag;

    std::string prefix, entity;
    instance.SplitEntityTag(it->first, &prefix, &entity);
    if (entity != "") entities.Insert(entity);
  }

  LOG(INFO) << "Entities:";
  for (Alphabet::iterator it = entities.begin(); it != entities.end(); ++it) {
    LOG(INFO) << it->first;
  }

  LOG(INFO) << "Computing allowed bigrams...";
  // Every bigram is allowed by default.
  allowed_bigrams_.assign(1 + tag_alphabet_.size(),
                          std::vector<bool>(1 + tag_alphabet_.size(), true));
  // Now add the BIO-like constraints.
  for (Alphabet::iterator it = entities.begin(); it != entities.end(); ++it) {
    std::string entity = it->first;
    LOG(INFO) << "Processing entity " << entity << "...";
    if (static_cast<EntityPipe*>(pipe_)->GetEntityOptions()->tagging_scheme() ==
        EntityTaggingSchemes::BIO) {
      int tag_begin = tag_alphabet_.Lookup("B-" + entity);
      int tag_inside = tag_alphabet_.Lookup("I-" + entity);
      if (tag_inside < 0) continue;
      // An I-tag can only occur after a B-tag or another I-tag of the same
      // entity.
      for (int left_tag = -1; left_tag < tag_alphabet_.size(); ++left_tag) {
        if (left_tag != tag_begin && left_tag != tag_inside) {
          allowed_bigrams_[1+tag_inside][1+left_tag] = false;
        }
      }
    } else if (static_cast<EntityPipe*>(pipe_)->GetEntityOptions()->
               tagging_scheme() == EntityTaggingSchemes::BILOU) {
      int tag_begin = tag_alphabet_.Lookup("B-" + entity);
      int tag_inside = tag_alphabet_.Lookup("I-" + entity);
      int tag_last = tag_alphabet_.Lookup("L-" + entity);
      int tag_unique = tag_alphabet_.Lookup("U-" + entity);
      // I-tags and L-tags can only occur after a B-tag or an I-tag of the same
      // entity.
      for (int left_tag = -1; left_tag < tag_alphabet_.size(); ++left_tag) {
        if (left_tag != tag_begin && left_tag != tag_inside) {
          if (tag_inside >= 0) {
            allowed_bigrams_[1+tag_inside][1+left_tag] = false;
          }
          if (tag_last >= 0) {
            allowed_bigrams_[1+tag_last][1+left_tag] = false;
          }
        }
      }
      // I-tags and B-tags can only occur before an I-tag or an L-tag of the
      // same entity.
      for (int right_tag = -1; right_tag < tag_alphabet_.size(); ++right_tag) {
        if (right_tag != tag_last && right_tag != tag_inside) {
          if (tag_inside >= 0) {
            allowed_bigrams_[1+right_tag][1+tag_inside] = false;
          }
          if (tag_begin >= 0) {
            allowed_bigrams_[1+right_tag][1+tag_begin] = false;
          }
        }
      }
    }
  }

  tag_alphabet_.BuildNames(); // Just to be able to plot readable information...
  int num_allowed_bigrams = 0;
  for (int tag = -1; tag < tag_alphabet_.size(); ++tag) {
    for (int left_tag = -1; left_tag < tag_alphabet_.size(); ++left_tag) {
      if (IsAllowedBigram(left_tag, tag)) {
        std::string left_tag_name = (left_tag >= 0)?
          tag_alphabet_.GetName(left_tag) : "START";
        std::string tag_name = (tag >= 0)?
          tag_alphabet_.GetName(tag) : "STOP";

        LOG(INFO) << "Allowed bigram: "
                  << left_tag_name
                  << " -> "
                  << tag_name;

        ++num_allowed_bigrams;
      }
    }
  }

  LOG(INFO) << "Total allowed bigrams: " << num_allowed_bigrams;

  ReadGazetteerFiles();
}

void EntityDictionary::ReadGazetteerFiles() {
  EntityOptions *options =
    static_cast<EntityOptions*>(pipe_->GetOptions());

  gazetteer_word_alphabet_.AllowGrowth();
  gazetteer_entity_tag_alphabet_.AllowGrowth();

  if (options->file_gazetteer() != "") {
    LOG(INFO) << "Loading gazetteer file "
              << options->file_gazetteer() << "...";
    std::ifstream is;
    is.open(options->file_gazetteer().c_str(), ifstream::in);
    CHECK(is.good()) << "Could not open "
                     << options->file_gazetteer() << ".";
    std::string line;
    if (is.is_open()) {
      while (!is.eof()) {
        getline(is, line);
        if (line == "") continue; // Ignore blank lines.
        std::vector<std::string> fields;
        StringSplit(line, " \t", &fields); // Break on tabs or spaces.
        if (fields.size() < 2) continue;
        const std::string &entity_type = fields[0];
        int entity_type_begin_id =
          gazetteer_entity_tag_alphabet_.Insert("B-" + entity_type);
        int entity_type_inside_id =
          gazetteer_entity_tag_alphabet_.Insert("I-" + entity_type);
        int entity_type_last_id =
          gazetteer_entity_tag_alphabet_.Insert("L-" + entity_type);
        int entity_type_unique_id =
          gazetteer_entity_tag_alphabet_.Insert("U-" + entity_type);
        for (int k = 1; k < fields.size(); ++k) {
          const std::string &word = fields[k];
          int word_id = gazetteer_word_alphabet_.Insert(word);
          if (gazetteer_word_entity_tags_.size() <= word_id) {
            gazetteer_word_entity_tags_.resize(word_id+1);
          }
          if (fields.size() == 2) {
            gazetteer_word_entity_tags_[word_id].
              push_back(entity_type_unique_id);
          } else if (k == 1) {
            gazetteer_word_entity_tags_[word_id].
              push_back(entity_type_begin_id);
          } else if (k == fields.size() - 1) {
            gazetteer_word_entity_tags_[word_id].
              push_back(entity_type_last_id);
          } else {
            gazetteer_word_entity_tags_[word_id].
              push_back(entity_type_inside_id);
          }
        }
      }
    }
  }

  gazetteer_word_alphabet_.StopGrowth();
  gazetteer_entity_tag_alphabet_.StopGrowth();
  LOG(INFO) << "Number of gazetteer words: "
            << gazetteer_word_alphabet_.size();
  LOG(INFO) << "Number of gazetteer entity tags: "
            << gazetteer_entity_tag_alphabet_.size();
}
